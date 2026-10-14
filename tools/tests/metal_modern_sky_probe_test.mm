#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "graphics/gta4_metal/modern_sky_probe.h"
#include <array>
#include <cstdio>
#include <limits>
#include <stdexcept>
using rex::graphics::gta4_metal::ModernSkyProbe;
static void Require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
int main(){@autoreleasepool{try{
  auto device=MTLCreateSystemDefaultDevice(); Require(device,"Metal device unavailable");
  auto queue=[device newCommandQueue]; NSError* failure=nil;
  auto options=[MTLCompileOptions new]; options.fastMathEnabled=NO;
  auto library=[device newLibraryWithSource:@R"metal(
    #include <metal_stdlib>
    using namespace metal;
    vertex float4 vs(uint i [[vertex_id]]) {
      const float2 p[]={float2(-1,-1),float2(3,-1),float2(-1,3)};
      return float4(p[i],0,1);
    }
    fragment float4 ps(constant float4& color [[buffer(0)]]) {return color;}
  )metal" options:options error:&failure];
  Require(library,failure?failure.localizedDescription.UTF8String:"library failed");
  ModernSkyProbe probe; unsigned cases=0;
  for(unsigned samples:{1u,4u}){
    Require([device supportsTextureSampleCount:samples],"sample count unavailable");
    auto desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:64 height:36 mipmapped:NO];
    desc.textureType=samples>1?MTLTextureType2DMultisample:MTLTextureType2D;
    desc.sampleCount=samples;desc.storageMode=MTLStorageModePrivate;
    desc.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
    auto image=[device newTextureWithDescriptor:desc];Require(image,"target allocation failed");
    auto pd=[MTLRenderPipelineDescriptor new];pd.vertexFunction=[library newFunctionWithName:@"vs"];
    pd.fragmentFunction=[library newFunctionWithName:@"ps"];pd.rasterSampleCount=samples;
    pd.colorAttachments[0].pixelFormat=desc.pixelFormat;
    auto pipeline=[device newRenderPipelineStateWithDescriptor:pd error:&failure];
    Require(pipeline,failure?failure.localizedDescription.UTF8String:"pipeline failed");
    for(unsigned mode=0;mode<5;++mode){
      auto commands=[queue commandBuffer];
      const auto pass=[&](bool clear,id<MTLBuffer> visibility){
        auto p=[MTLRenderPassDescriptor renderPassDescriptor];
        p.colorAttachments[0].texture=image;p.colorAttachments[0].loadAction=clear?MTLLoadActionClear:MTLLoadActionLoad;
        p.colorAttachments[0].clearColor=MTLClearColorMake(0.25,0.5,0.75,1);
        p.colorAttachments[0].storeAction=MTLStoreActionStore;p.visibilityResultBuffer=visibility;
        return p;
      };
      auto initialize=[commands renderCommandEncoderWithDescriptor:pass(true,nil)];
      if(mode==4){
        const std::array<float,4> invalid{std::numeric_limits<float>::quiet_NaN(),0,0,1};
        [initialize setRenderPipelineState:pipeline];[initialize setFragmentBytes:invalid.data() length:sizeof(invalid) atIndex:0];
        [initialize drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
      }
      [initialize endEncoding];
      std::string error;
      auto capture=probe.Start(device,commands,image,"fixture",error);Require(bool(capture),error);
      auto draw=[commands renderCommandEncoderWithDescriptor:pass(false,capture->visibility)];
      [draw setRenderPipelineState:pipeline];[draw setFrontFacingWinding:MTLWindingCounterClockwise];
      [draw setCullMode:mode==2?MTLCullModeFront:MTLCullModeNone];
      std::array<float,4> color{1,0.125,0.0625,1};
      if(mode==1)color={0,0,0,1};
      if(mode==3)color[0]=std::numeric_limits<float>::quiet_NaN();
      [draw setFragmentBytes:color.data() length:sizeof(color) atIndex:0];
      [draw setVisibilityResultMode:MTLVisibilityResultModeCounting offset:0];
      [draw drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
      [draw endEncoding];capture->queried=true;
      Require(probe.Finish(commands,image,capture,error),error);
      const auto observation=*capture;
      const std::weak_ptr<ModernSkyProbe::Capture> lifetime=capture;
      capture.reset();
      // The actual completion handler must own the capture independently of
      // this local, exactly as it must after the production draw returns.
      Require(!lifetime.expired(),"GPU completion handler lost its capture owner");
      [commands commit];[commands waitUntilCompleted];
      Require(commands.status==MTLCommandBufferStatusCompleted,commands.error?commands.error.localizedDescription.UTF8String:"GPU failure");
      const auto stats=ModernSkyProbe::Read(observation);
      Require(stats.visible_samples==(mode==2?0:uint64_t(desc.width)*desc.height*samples),"visibility count mismatch");
      Require(stats.changed==(mode==2?0:ModernSkyProbe::kSamples),"changed sample mismatch");
      Require(stats.black==(mode==1?ModernSkyProbe::kSamples:0),"black sample mismatch");
      Require(stats.invalid_after==(mode==3?ModernSkyProbe::kSamples:0),"invalid output count mismatch");
      Require(stats.invalid_before==(mode==4?ModernSkyProbe::kSamples:0),"invalid input count mismatch");
      ++cases;
      std::printf("PASS samples=%u fixture=%u visible=%llu changed=%u black=%u invalid=%u/%u\n",samples,mode,
          stats.visible_samples,stats.changed,stats.black,stats.invalid_before,stats.invalid_after);
    }
  }
  std::printf("PASS %u Metal sky probe GPU fixtures\n",cases);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}}}
