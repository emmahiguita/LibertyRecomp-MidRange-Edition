#include <cmath>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "graphics/gta4_metal/encoder_bindings.h"
#include <array>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
using rex::graphics::gta4_metal::EncoderBindings;
namespace {size_t checks=0;
void Check(bool b,const std::string& why){++checks;if(!b)throw std::runtime_error(why);}
std::array<float,4> Color(size_t i){return {float((i*73)%251)/255,float((i*37)%241)/255,float((i*17)%239)/255,1};}}
int main(){@autoreleasepool{try{
 auto device=MTLCreateSystemDefaultDevice();auto queue=[device newCommandQueue];NSError* e=nil;
 NSString* source=@R"MSL(#include <metal_stdlib>
using namespace metal;
struct Push {device const float4* color;ulong unused1,unused2;};
vertex float4 vs(uint id [[vertex_id]]){float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};return float4(p[id],0,1);}
fragment float4 ps(constant Push& p [[buffer(8)]]){return *p.color;}
)MSL";
 auto lib=[device newLibraryWithSource:source options:nil error:&e];Check(lib!=nil,e?e.localizedDescription.UTF8String:"library");
 auto d=[MTLRenderPipelineDescriptor new];d.vertexFunction=[lib newFunctionWithName:@"vs"];d.fragmentFunction=[lib newFunctionWithName:@"ps"];d.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA8Unorm;
 auto pipeline=[device newRenderPipelineStateWithDescriptor:d error:&e];Check(pipeline!=nil,e?e.localizedDescription.UTF8String:"pipeline");
 for(bool cache:{false,true})for(size_t count:{1u,255u,256u,257u,512u}){
  auto desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:count height:2 mipmapped:NO];desc.storageMode=MTLStorageModePrivate;desc.usage=MTLTextureUsageRenderTarget;
  auto target=[device newTextureWithDescriptor:desc];auto commands=[queue commandBuffer];Check(commands.retainedReferences,"test must use retained commands");
  std::vector<id<MTLBuffer>> buffers;buffers.reserve(count);
  for(size_t i=0;i<count;++i){const auto color=Color(i);buffers.push_back([device newBufferWithBytes:color.data() length:sizeof(color) options:MTLResourceStorageModeShared]);Check(buffers.back()!=nil,"buffer allocation");}
  EncoderBindings bindings;
  for(size_t pass_index=0;pass_index<2;++pass_index){
   auto pass=[MTLRenderPassDescriptor renderPassDescriptor];pass.colorAttachments[0].texture=target;pass.colorAttachments[0].loadAction=pass_index?MTLLoadActionLoad:MTLLoadActionClear;pass.colorAttachments[0].storeAction=MTLStoreActionStore;
   auto encoder=[commands renderCommandEncoderWithDescriptor:pass];Check(encoder!=nil,"encoder");bindings.Reset(cache);bindings.Pipeline(encoder,pipeline);bindings.Viewport(encoder,{0,0,double(count),2,0,1});
   for(size_t i=0;i<count;++i){
    // Deliberately overfill the memo; a collision may cause extra useResources,
    // but never omission of an actual resource. Second encoder needs fresh state.
    std::array<id<MTLResource>,3> required{buffers[i],nil,buffers[i]};
    bindings.ReadResources(encoder,required.data(),required.size());bindings.ReadResources(encoder,required.data(),required.size());
    bindings.Push(encoder,{buffers[i].gpuAddress,0,0});bindings.Scissor(encoder,{i,pass_index,1,1});
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
   }
   [encoder endEncoding];bindings.Reset(cache);
  }
  __weak id<MTLBuffer> witness=buffers.front();buffers.clear();Check(witness!=nil,"cache removal released an encoded resource");
  const size_t pitch=(count*4+255)&~size_t(255);auto result=[device newBufferWithLength:pitch*2 options:MTLResourceStorageModeShared];auto blit=[commands blitCommandEncoder];
  [blit copyFromTexture:target sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(count,2,1) toBuffer:result destinationOffset:0 destinationBytesPerRow:pitch destinationBytesPerImage:pitch*2];[blit endEncoding];
  [commands commit];[commands waitUntilCompleted];Check(commands.status==MTLCommandBufferStatusCompleted,commands.error?commands.error.localizedDescription.UTF8String:"GPU execution");
  for(size_t y=0;y<2;++y)for(size_t i=0;i<count;++i)for(size_t c=0;c<4;++c){auto actual=static_cast<const uint8_t*>(result.contents)[y*pitch+i*4+c];Check(std::abs(int(actual)-int(std::lround(Color(i)[c]*255)))<=1,"indirect resource read changed after releasing CPU cache owners");}
  std::printf("encoder_residency cache=%u resources=%zu calls=%llu skipped=%llu passed=true\n",cache,count,bindings.residency_calls,bindings.residency_skipped);
 }
 std::printf("metal_encoder_residency_gpu=passed checks=%zu\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"Encoder residency: %s\n",e.what());return 1;}}}
