#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <mach-o/dyld.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#include <rex/cvar.h>
#include <rex/graphics/gta4_native/options.h>
#include <rex/ui/presenter.h>
#include "ui/metal/context.h"
#include "ui/metal/presentation_effects.h"
#include "graphics/gta4_metal/guest_state.h"

static void Require(bool value, const std::string& message) {
  if (!value) throw std::runtime_error(message);
}

struct PresenterContracts : rex::ui::Presenter {
  using Presenter::BilinearConstants;
  using Presenter::CasSharpenConstants;
  using Presenter::CasResampleConstants;
  using Presenter::FsrEasuConstants;
  using Presenter::FsrRcasConstants;
  using Presenter::GuestOutputPaintFlow;
};

static void TestSettings() {
  using namespace rex::cvar;
  struct Setting { const char* name; const char* value; };
  constexpr Setting expected[] = {
    {"gta4_native_vector_fonts","true"}, {"gta4_native_spatial_aa","true"},
    {"gta4_native_output_dither","true"}, {"gta4_native_hdr_high_precision","true"},
    {"gta4_texture_filtering","trilinear"}, {"gta4_anisotropic_filtering","1x"},
    {"gta4_native_msaa","4x"}, {"gta4_native_light_overrides","pair"},
    {"gta4_native_host_sun_shafts","false"}, {"gta4_native_host_fog","false"},
    {"gta4_native_frames_in_flight","2"}, {"gta4_modern_shaders","false"},
    {"gta4_trace_modern_shaders","false"}
  };
  for (uint32_t i=0; i<_dyld_image_count(); ++i)
    Require(std::strstr(_dyld_get_image_name(i),"librexgpu-")==nullptr,
            "Settings test unexpectedly loaded a renderer plugin");
  for (const auto& setting : expected) {
    const auto* entry = GetFlagInfo(setting.name);
    Require(entry && entry->default_value == setting.value, std::string("Missing/changed shared setting: ")+setting.name);
  }
  Require(SetFlagByName("gta4_texture_filtering","bilinear"), "Cannot change filtering");
  Require(REXCVAR_GET(gta4_texture_filtering)=="bilinear", "Registry and exported accessor have different storage");
  Require(!SetFlagByName("gta4_anisotropic_filtering","3x"), "Invalid filtering value accepted");
  Require(SetFlagByName("gta4_anisotropic_filtering","16x"), "Cannot change anisotropy");
  Require(SetFlagByName("gta4_modern_shaders","true") && Query<bool>("gta4_modern_shaders"), "Modern shader toggle missing");
  Require(GetFlagInfo("gta4_native_msaa")->lifecycle == Lifecycle::kRequiresRestart, "MSAA restart contract changed");
  Require(GetFlagInfo("gta4_texture_filtering")->lifecycle == Lifecycle::kHotReload, "Filtering hot reload contract changed");
  for (const auto& setting : expected) Require(SetFlagByName(setting.name,setting.value),"Cannot restore setting");
  ClearPendingRestartFlags();
  std::printf("shared_settings=%zu plugin_loaded=false passed=true\n", std::size(expected));
}

static void TestGuestState() {
  using namespace rex::graphics;
  std::vector<uint8_t> bytes(gta4_native::kGuestDeviceSize);
  auto put = [&](size_t offset, uint32_t value) { value=__builtin_bswap32(value); std::memcpy(bytes.data()+offset,&value,sizeof(value)); };
  put(11868,1); put(10548,(5u<<4)|(1u<<2)|(1u<<7)|(3u<<8)); put(10568,4);
  put(10460,0x9A5F); put(10556,8|6); put(10500,std::bit_cast<uint32_t>(0.5f));
  bytes[10499]=0x37; bytes[10498]=0xAA; bytes[10497]=0x55;
  put(12648,std::bit_cast<uint32_t>(1920.0f));
  const auto state = gta4_metal::DecodeFixedState(bytes);
  Require(state.depth_enable==1 && state.depth_function==5 && state.depth_write_enable==1,"Depth decode differs from retail fields");
  Require(state.stencil_function==3 && state.stencil_reference==0x37 && state.stencil_mask==0xAA && state.stencil_write_mask==0x55,"Stencil decode differs");
  Require(state.alpha_test_enable==1 && state.alpha_function==6 && state.alpha_reference==0.5f,"Alpha decode differs");
  Require(state.color_write_mask==0x9A5F && state.viewport_bits[2]==std::bit_cast<uint32_t>(1920.0f),"Color or viewport decode differs");
  Require(gta4_metal::DecodeFixedState({}).depth_enable==0,"Short state was not rejected");
  Require(gta4_metal::TitleCommandSize(gta4_native::CommandType::kResolve)==sizeof(gta4_native::ResolveCommand),"Resolve ABI differs");
  Require(gta4_metal::TitleCommandSize(gta4_native::CommandType(UINT32_MAX))==0,"Unknown command accepted");
  std::puts("guest_state=passed");
}

static void TestEffects(const char* library_path) {
  using namespace rex::ui::metal;
  std::string error;
  auto context=MetalContext::Create(error); Require(bool(context),error);
  NSError* native_error=nil;
  context->pass_library=[context->device newLibraryWithURL:[NSURL fileURLWithPath:@(library_path)] error:&native_error];
  Require(context->pass_library!=nil,MetalError(native_error,"Pass library load failed"));
  PresentationEffects effects; Require(effects.Initialize(context,error),error);
  constexpr NSUInteger input_size=8, target_size=24;
  const std::array<float,4> color={0.25f,0.5f,0.75f,1.0f};
  std::vector<std::array<float,4>> input_pixels(input_size*input_size,color);
  auto source_desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:input_size height:input_size mipmapped:NO];
  source_desc.storageMode=MTLStorageModeShared; source_desc.usage=MTLTextureUsageShaderRead;
  auto source=[context->device newTextureWithDescriptor:source_desc]; Require(source!=nil,"Source allocation failed");
  [source replaceRegion:MTLRegionMake2D(0,0,input_size,input_size) mipmapLevel:0 withBytes:input_pixels.data() bytesPerRow:input_size*sizeof(color)];
  size_t cases=0;
  for (const auto format : {MTLPixelFormatBGRA8Unorm,MTLPixelFormatRGBA16Float}) {
    for (size_t effect_index=0; effect_index<size_t(PresentationEffect::kCount); ++effect_index) {
      const auto effect=PresentationEffect(effect_index);
      const bool easu=effect==PresentationEffect::kFsrEasu;
      const uint32_t extent=easu ? target_size : input_size;
      const int32_t offset=easu ? 0 : 4;
      PresenterContracts::GuestOutputPaintFlow flow{};
      flow.effect_count=1; flow.properties.frontbuffer_width=input_size; flow.properties.frontbuffer_height=input_size;
      flow.effect_output_sizes[0]={extent,extent}; flow.output_x=flow.output_y=offset;
      PresenterContracts::GuestOutputPaintConfig config;
      std::array<std::byte,48> constants{}; size_t constant_size=0;
      auto store=[&](const auto& value) { constant_size=sizeof(value); std::memcpy(constants.data(),&value,sizeof(value)); };
      switch (effect) {
        case PresentationEffect::kBilinear: case PresentationEffect::kBilinearDither: {
          PresenterContracts::BilinearConstants value{}; value.Initialize(flow,0); store(value); break;
        }
        case PresentationEffect::kCasSharpen: case PresentationEffect::kCasSharpenDither: {
          PresenterContracts::CasSharpenConstants value{}; value.Initialize(flow,0,config); store(value); break;
        }
        case PresentationEffect::kCasResample: case PresentationEffect::kCasResampleDither: {
          PresenterContracts::CasResampleConstants value{}; value.Initialize(flow,0,config); store(value); break;
        }
        case PresentationEffect::kFsrEasu: {
          PresenterContracts::FsrEasuConstants value{}; value.Initialize(flow,0); store(value); break;
        }
        default: {
          PresenterContracts::FsrRcasConstants value{}; value.Initialize(flow,0,config); store(value); break;
        }
      }
      auto desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:target_size height:target_size mipmapped:NO];
      desc.storageMode=MTLStorageModePrivate; desc.usage=MTLTextureUsageRenderTarget;
      auto target=[context->device newTextureWithDescriptor:desc]; Require(target!=nil,"Target allocation failed");
      auto pass=[MTLRenderPassDescriptor renderPassDescriptor];
      pass.colorAttachments[0].texture=target; pass.colorAttachments[0].loadAction=MTLLoadActionClear;
      pass.colorAttachments[0].storeAction=MTLStoreActionStore; pass.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,1);
      auto commands=[context->queue commandBuffer]; auto encoder=[commands renderCommandEncoderWithDescriptor:pass];
      Require(effects.Draw(encoder,effect,source,target,MTLViewport{double(offset),double(offset),double(extent),double(extent),0,1},
              std::span(constants.data(),constant_size),error),error);
      [encoder endEncoding];
      const size_t pixel_bytes=format==MTLPixelFormatRGBA16Float ? 8 : 4;
      const size_t pitch=(target_size*pixel_bytes+255)&~size_t(255);
      auto readback=[context->device newBufferWithLength:pitch*target_size options:MTLResourceStorageModeShared];
      auto blit=[commands blitCommandEncoder];
      [blit copyFromTexture:target sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
          sourceSize:MTLSizeMake(target_size,target_size,1) toBuffer:readback destinationOffset:0
          destinationBytesPerRow:pitch destinationBytesPerImage:pitch*target_size];
      [blit endEncoding]; [commands commit]; [commands waitUntilCompleted];
      Require(commands.status==MTLCommandBufferStatusCompleted,MetalError(commands.error,"GPU execution failed"));
      auto component=[&](size_t x,size_t y,size_t channel) {
        auto bytes=static_cast<const uint8_t*>(readback.contents)+y*pitch+x*pixel_bytes;
        if (format==MTLPixelFormatRGBA16Float) { _Float16 v; std::memcpy(&v,bytes+channel*sizeof(v),sizeof(v)); return float(v); }
        constexpr size_t bgra[]={2,1,0,3}; return float(bytes[bgra[channel]])/255.0f;
      };
      // Interior color correctness and every exterior pixel's letterbox isolation.
      for (size_t channel=0;channel<4;++channel) {
        const float value=component(offset+extent/2,offset+extent/2,channel);
        Require(std::isfinite(value) && std::abs(value-color[channel])<0.015f,
            "Presentation color mismatch: effect="+std::to_string(effect_index)+" channel="+std::to_string(channel)+" actual="+std::to_string(value));
      }
      for(size_t y=0;y<target_size;++y) for(size_t x=0;x<target_size;++x) {
        if(x>=size_t(offset) && y>=size_t(offset) && x<size_t(offset)+extent && y<size_t(offset)+extent) continue;
        for(size_t channel=0;channel<3;++channel) Require(component(x,y,channel)==0,"Presentation escaped the letterbox scissor");
      }
      ++cases;
    }
  }
  std::printf("presentation_gpu_cases=%zu device=%s passed=true\n",cases,context->device.name.UTF8String);
}

int main(int argc,const char** argv) {
  @autoreleasepool {
    try {
      Require(argc==2,"usage: rex-metal-frontend-test passes.metallib");
      TestSettings(); TestGuestState(); TestEffects(argv[1]);
      return 0;
    } catch(const std::exception& error) { std::fprintf(stderr,"Metal test: %s\n",error.what()); return 1; }
  }
}
