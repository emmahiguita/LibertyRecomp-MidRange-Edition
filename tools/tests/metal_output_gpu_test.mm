#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "graphics/gta4_metal/post_processing.h"
#include "ui/metal/context.h"
#include "ui/metal/output_transfer.h"
#include "ui/metal/presentation_effects.h"
#include "ui/metal/presentation_image_cache.h"
#include "ui/metal/immediate_drawer.h"
#include "ui/metal/ui_draw_context.h"
#include <rex/ui/presenter.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context_sdl.h>
#include <rex/graphics/gta4_native/upscaling_policy.h>
#include <rex/graphics/gta4_native/supersampling_policy.h>

namespace {
using namespace rex::graphics::gta4_metal;
using namespace rex::graphics::gta4_native;
using namespace rex::ui::metal;
using Pixel = std::array<float, 4>;
size_t checks = 0, cases = 0;
void Check(bool value, const std::string& message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
struct Fixture {
  std::shared_ptr<MetalContext> context;
  PostProcessing post;
  OutputTransfer transfer;
  std::string error;
  explicit Fixture(const char* library) {
    context = MetalContext::Create(error); Check(bool(context), error);
    NSError* native_error = nil;
    context->pass_library = [context->device newLibraryWithURL:
        [NSURL fileURLWithPath:[NSString stringWithUTF8String:library]] error:&native_error];
    Check(context->pass_library != nil, MetalError(native_error, "Utility library unavailable"));
    Check(post.Initialize(context, error), error);
    Check(transfer.Initialize(context, error), error);
  }
  id<MTLTexture> Source(NSUInteger width, const std::vector<Pixel>& pixels) {
    auto descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
        width:width height:width mipmapped:NO];
    descriptor.storageMode = MTLStorageModeShared;
    descriptor.usage = MTLTextureUsageShaderRead;
    auto texture = [context->device newTextureWithDescriptor:descriptor];
    Check(texture != nil && pixels.size() == width * width, "Invalid test source");
    [texture replaceRegion:MTLRegionMake2D(0, 0, width, width) mipmapLevel:0
        withBytes:pixels.data() bytesPerRow:width * sizeof(Pixel)];
    return texture;
  }
  id<MTLTexture> Target(NSUInteger width) {
    auto descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
        width:width height:width mipmapped:NO];
    descriptor.storageMode = MTLStorageModePrivate;
    descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    auto target = [context->device newTextureWithDescriptor:descriptor]; Check(target != nil, "Target allocation failed");
    return target;
  }
  std::vector<Pixel> Read(id<MTLCommandBuffer> commands, id<MTLTexture> image) {
    const size_t pitch = (image.width * 8 + 255) & ~size_t(255);
    auto buffer = [context->device newBufferWithLength:pitch * image.height options:MTLResourceStorageModeShared];
    Check(buffer != nil, "Readback allocation failed");
    auto blit = [commands blitCommandEncoder]; Check(blit != nil, "Readback encoder failed");
    [blit copyFromTexture:image sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
        sourceSize:MTLSizeMake(image.width, image.height, 1) toBuffer:buffer destinationOffset:0
        destinationBytesPerRow:pitch destinationBytesPerImage:pitch * image.height];
    [blit endEncoding]; [commands commit]; [commands waitUntilCompleted];
    Check(commands.status == MTLCommandBufferStatusCompleted, MetalError(commands.error, "GPU failure"));
    std::vector<Pixel> values(image.width * image.height);
    for (size_t y = 0; y < image.height; ++y) for (size_t x = 0; x < image.width; ++x) {
      for (size_t c = 0; c < 4; ++c) {
        _Float16 v;
        std::memcpy(&v, static_cast<const uint8_t*>(buffer.contents) + y * pitch + x * 8 + c * sizeof(v), sizeof(v));
        values[y * image.width + x][c] = float(v);
        Check(std::isfinite(float(v)), "Nonfinite output pixel");
      }
    }
    ++cases;
    return values;
  }
};

void TestAntiAliasing(Fixture& f) {
  for (NSUInteger width : {16u, 32u, 24u}) {
    const Pixel color{0.25f, 0.5f, 0.75f, 1.0f};
    auto source = f.Source(width, std::vector<Pixel>(width * width, color));
    auto target = f.Target(width);
    for (auto mode : {AntiAliasingMode::kOff, AntiAliasingMode::kFxaa, AntiAliasingMode::kSmaa, AntiAliasingMode::kMsaa4xSmaa}) {
      for (std::string_view quality : {"low", "medium", "high", "ultra"}) {
        auto commands = [f.context->queue commandBuffer];
        Check(f.post.Record(commands, source, target, mode, quality, false, f.error), f.error);
        const auto result = f.Read(commands, target);
        for (const auto& value : result) for (size_t c = 0; c < 4; ++c)
          Check(std::abs(value[c] - color[c]) < 0.003f, "AA altered a uniform image");
      }
    }
    const size_t reserved = f.post.allocated_bytes();
    auto commands = [f.context->queue commandBuffer];
    Check(f.post.Record(commands, source, target, AntiAliasingMode::kSmaa, "high", false, f.error), f.error);
    f.Read(commands, target);
    Check(f.post.allocated_bytes() == reserved, "SMAA extent resources grew on reuse");
    f.post.ReleaseExtentResources();
    Check(f.post.allocated_bytes() < reserved, "SMAA extent resources not released");
  }
  constexpr NSUInteger width = 32;
  std::vector<Pixel> staircase(width * width);
  for (size_t y = 0; y < width; ++y) for (size_t x = 0; x < width; ++x) {
    const float value = x > y / 2 + 7 ? 1.0f : 0.0f;
    staircase[y * width + x] = {value, value, value, 1};
  }
  auto source = f.Source(width, staircase), target = f.Target(width);
  for (auto mode : {AntiAliasingMode::kFxaa, AntiAliasingMode::kSmaa, AntiAliasingMode::kMsaa4xSmaa}) {
    auto commands = [f.context->queue commandBuffer];
    Check(f.post.Record(commands, source, target, mode, "high", false, f.error), f.error);
    const auto result = f.Read(commands, target);
    size_t changed = 0;
    for (size_t i = 0; i < result.size(); ++i) if (std::abs(result[i][0] - staircase[i][0]) > 0.005f) ++changed;
    Check(changed > 0, "Selected AA mode did not process the edge");
    std::printf("aa_edge mode=%u modified_pixels=%zu\n", unsigned(mode), changed);
  }
  std::vector<Pixel> checker(width * width);
  for (size_t y = 0; y < width; ++y) for (size_t x = 0; x < width; ++x) {
    const float v = (x + y) & 1 ? 1.0f : 0.0f; checker[y * width + x] = {v, v, v, 1};
  }
  auto ssaa_source = f.Source(width, checker), ssaa_target = f.Target(width / 2);
  auto commands = [f.context->queue commandBuffer];
  Check(f.post.Record(commands, ssaa_source, ssaa_target, AntiAliasingMode::kSsaa4x, "high", false, f.error), f.error);
  const auto values = f.Read(commands, ssaa_target);
  // Python-derived golden: 1.055 * (0.5 ** (1 / 2.4)) - 0.055.
  for (const auto& p : values) Check(std::abs(p[0] - 0.7353569830524495f) < 0.003f,
                                    "SSAA averaged perceptual values instead of linear light");
}

void TestHdr(Fixture& f) {
  constexpr NSUInteger width = 16;
  for (float value : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
    auto source = f.Source(width, std::vector<Pixel>(width * width, Pixel{value, value, value, 1}));
    for (uint32_t mode : {0u, 1u, 2u}) for (float headroom : {1.0f, 2.0f, 4.0f}) {
      auto target = f.Target(width);
      auto commands = [f.context->queue commandBuffer];
      auto pass = [MTLRenderPassDescriptor renderPassDescriptor];
      pass.colorAttachments[0].texture = target;
      pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
      pass.colorAttachments[0].storeAction = MTLStoreActionStore;
      auto encoder = [commands renderCommandEncoderWithDescriptor:pass];
      OutputTransferConstants constants{}; constants.hdr_mode = mode; constants.hdr_headroom = headroom;
      Check(f.transfer.Draw(encoder, source, target, constants, f.error), f.error);
      [encoder endEncoding];
      auto result = f.Read(commands, target);
      const double linear = value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
      double expected = value;
      if (mode) {
        expected = linear;
        if (mode == 2 && linear > 0.000001)
          expected += std::pow(linear, 2.5) * (std::max(1.0, 400.0 / 203.0) - 1.0);
        expected = std::min(expected * 203.0 / 80.0, double(headroom));
      }
      for (const auto& p : result) for (size_t c = 0; c < 3; ++c)
        Check(std::abs(p[c] - expected) < 0.006, "HDR output differs from the authored transfer function");
    }
  }
}

struct PresentationConstants : rex::ui::Presenter {
  using Presenter::BilinearConstants;
  using Presenter::GuestOutputPaintFlow;
};

void TestDirectHdrInput(Fixture& f) {
  PresentationEffects effects; Check(effects.Initialize(f.context, f.error), f.error);
  float maximum_difference = 0;
  for (NSUInteger width : {17u, 64u, 257u}) {
    std::vector<std::array<_Float16, 4>> pixels(width * width);
    for (size_t y = 0; y < width; ++y) for (size_t x = 0; x < width; ++x) {
      const float r = float(x) / float(width - 1);
      const float g = float(y) / float(width - 1);
      pixels[y * width + x] = {_Float16(r), _Float16(g), _Float16((x + y) % 2), _Float16(1)};
    }
    auto descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
        width:width height:width mipmapped:NO];
    descriptor.storageMode = MTLStorageModeShared; descriptor.usage = MTLTextureUsageShaderRead;
    auto source = [f.context->device newTextureWithDescriptor:descriptor];
    Check(source != nil, "Direct HDR source allocation failed");
    [source replaceRegion:MTLRegionMake2D(0, 0, width, width) mipmapLevel:0
        withBytes:pixels.data() bytesPerRow:width * sizeof(pixels[0])];
    auto copy = f.Target(width), target = f.Target(width);
    for (uint32_t mode : {1u, 2u}) for (float headroom : {1.0f, 2.0f, 4.0f}) {
      std::array<std::vector<Pixel>, 2> results;
      for (size_t path = 0; path < results.size(); ++path) {
        auto commands = [f.context->queue commandBuffer];
        const auto encoder_for = [&](id<MTLTexture> texture) {
          auto pass = [MTLRenderPassDescriptor renderPassDescriptor];
          pass.colorAttachments[0].texture = texture;
          pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
          pass.colorAttachments[0].storeAction = MTLStoreActionStore;
          auto encoder = [commands renderCommandEncoderWithDescriptor:pass];
          Check(encoder != nil, "Direct HDR test encoder failed");
          return encoder;
        };
        if (path == 0) {
          PresentationConstants::GuestOutputPaintFlow flow{};
          flow.effect_count = 1; flow.properties.frontbuffer_width = uint32_t(width);
          flow.properties.frontbuffer_height = uint32_t(width); flow.effect_output_sizes[0] = {uint32_t(width), uint32_t(width)};
          PresentationConstants::BilinearConstants values{}; values.Initialize(flow, 0);
          auto encoder = encoder_for(copy);
          Check(effects.Draw(encoder, PresentationEffect::kBilinear, source, copy,
              MTLViewport{0, 0, double(width), double(width), 0, 1},
              std::as_bytes(std::span(&values, 1)), f.error), f.error);
          [encoder endEncoding];
        }
        auto encoder = encoder_for(target);
        OutputTransferConstants hdr{}; hdr.hdr_mode = mode; hdr.hdr_headroom = headroom;
        Check(f.transfer.Draw(encoder, path == 0 ? copy : source, target, hdr, f.error), f.error);
        [encoder endEncoding]; results[path] = f.Read(commands, target);
      }
      for (size_t pixel = 0; pixel < results[0].size(); ++pixel) for (size_t channel = 0; channel < 4; ++channel) {
        const float difference = std::abs(results[0][pixel][channel] - results[1][pixel][channel]);
        maximum_difference = std::max(maximum_difference, difference);
        Check(difference < 0.006f, "Removing an identity HDR input copy altered the output");
      }
    }
  }
  std::printf("direct_hdr_input=passed maximum_difference=%g\n", maximum_difference);
}

#include "metal_fused_hdr_cases.inc"
#include "metal_upscaling_features.inc"
#include "metal_ssaa_features.inc"
#include "metal_presentation_cache_cases.inc"

}

int main(int argc, char** argv) {
  @autoreleasepool {
    try {
      Check(argc==2||(argc==3&&std::string(argv[2])=="--benchmark-fused-hdr"),"usage: rex-metal-output-test passes.metallib [--benchmark-fused-hdr]");
      if(argc==3){Fixture f(argv[1]);BenchmarkFusedBilinearHdr(f);return 0;}
      Fixture fixture(argv[1]); TestAntiAliasing(fixture); TestHdr(fixture); TestDirectHdrInput(fixture); TestFusedBilinearHdr(fixture); TestUpscalingPolicy(); TestUpscalingChain(fixture); TestSupersamplingFactors(fixture);
      TestPresentationCachePolicy(); TestPresentationCachePixels(fixture); TestPresentationCacheQueuedLifetime(fixture);
      std::printf("metal_output_gpu=passed cases=%zu checks=%zu device=%s\n", cases, checks,
                   fixture.context->device.name.UTF8String);
      return 0;
    } catch (const std::exception& error) {
      std::fprintf(stderr, "Metal output test: %s\n", error.what()); return 1;
    }
  }
}
