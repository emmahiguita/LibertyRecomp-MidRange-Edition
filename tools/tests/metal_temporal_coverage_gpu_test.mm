#include "graphics/gta4_metal/temporal/motion_coverage.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace motion_coverage_test {
using rex::graphics::gta4_metal::temporal::Inputs;
using rex::graphics::gta4_metal::temporal::MotionCoverage;

struct Checks {
  size_t checks = 0, cases = 0;
  void Require(bool value, const std::string& message) {
    ++checks;
    if (!value) throw std::runtime_error("motion coverage: " + message);
  }
};
struct Pixels {
  NSUInteger width, height;
  std::vector<uint8_t> reactive;
  std::vector<std::array<_Float16, 2>> motion;
  std::vector<float> depth, previous;
  Pixels(NSUInteger w, NSUInteger h)
      : width(w), height(h), reactive(w * h, 0), motion(w * h, {0, 0}),
        depth(w * h, 0.5f), previous(w * h, 0.5f) {}
};
static_assert(sizeof(std::array<_Float16, 2>) == 4);

id<MTLTexture> Image(id<MTLDevice> device, MTLPixelFormat format, NSUInteger width,
                     NSUInteger height, Checks& test) {
  auto d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                             width:width height:height
                                                         mipmapped:NO];
  d.storageMode = MTLStorageModeShared;
  d.hazardTrackingMode = MTLHazardTrackingModeTracked;
  d.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
  auto result = [device newTextureWithDescriptor:d];
  test.Require(result != nil, "input allocation");
  return result;
}
Inputs Upload(id<MTLDevice> device, const Pixels& pixels, Checks& test) {
  Inputs in;
  in.reactive = Image(device, MTLPixelFormatR8Unorm, pixels.width, pixels.height, test);
  in.motion = Image(device, MTLPixelFormatRG16Float, pixels.width, pixels.height, test);
  in.depth = Image(device, MTLPixelFormatR32Float, pixels.width, pixels.height, test);
  in.previous_depth = Image(device, MTLPixelFormatR32Float, pixels.width, pixels.height, test);
  const auto region = MTLRegionMake2D(0, 0, pixels.width, pixels.height);
  [in.reactive replaceRegion:region mipmapLevel:0 withBytes:pixels.reactive.data()
                bytesPerRow:pixels.width * sizeof(pixels.reactive[0])];
  [in.motion replaceRegion:region mipmapLevel:0 withBytes:pixels.motion.data()
              bytesPerRow:pixels.width * sizeof(pixels.motion[0])];
  [in.depth replaceRegion:region mipmapLevel:0 withBytes:pixels.depth.data()
             bytesPerRow:pixels.width * sizeof(pixels.depth[0])];
  [in.previous_depth replaceRegion:region mipmapLevel:0 withBytes:pixels.previous.data()
                      bytesPerRow:pixels.width * sizeof(pixels.previous[0])];
  return in;
}
void Submit(id<MTLCommandBuffer> cb, Checks& test) {
  [cb commit];
  [cb waitUntilCompleted];
  test.Require(cb.status == MTLCommandBufferStatusCompleted,
               cb.error ? cb.error.localizedDescription.UTF8String : "GPU completion");
}
}  // namespace motion_coverage_test

// Linked into metal_temporal_gpu_test.mm by the test target. No main renderer,
// MetalFX interpolator, or presentation scheduler is needed for these cases.
void TestMotionCoverage(id<MTLDevice> device, id<MTLCommandQueue> queue,
                       id<MTLLibrary> library) {
  using namespace motion_coverage_test;
  Checks test;
  MotionCoverage coverage;
  std::string error;
  test.Require(!coverage.Complete(), "unencoded input was accepted");
  const auto run = [&](NSUInteger width, NSUInteger height, const std::string& name,
                       const std::function<void(Pixels&)>& mutate, bool expected) {
    Pixels pixels(width, height);
    if (mutate) mutate(pixels);
    const auto in = Upload(device, pixels, test);
    auto cb = [queue commandBuffer];
    test.Require(coverage.Encode(cb, in, library, error), name + ": " + error);
    test.Require(!coverage.Complete(), name + ": accepted before GPU completion");
    Submit(cb, test);
    test.Require(coverage.Complete() == expected, name + ": admission disagrees");
    ++test.cases;
  };
  for (const auto extent : {std::array<NSUInteger, 2>{1, 1}, {17, 19}, {32, 32}, {65, 33}})
    run(extent[0], extent[1], "complete image", {}, true);
  coverage.Invalidate();
  test.Require(!coverage.Complete(), "previous frame survived invalidation");

  // Place a single bad sample at both extremes and inside an odd-sized image.
  // This also tests that later good groups cannot erase another group's veto.
  constexpr NSUInteger width = 17, height = 19;
  for (size_t location : {size_t(0), size_t(width * height / 2), size_t(width * height - 1)}) {
    run(width, height, "reactive color with valid underlying geometry", [=](Pixels& p) { p.reactive[location] = 255; }, true);
    run(width, height, "fractional reactive color", [=](Pixels& p) { p.reactive[location] = 1; }, true);
    run(width, height, "nonfinite motion X", [=](Pixels& p) {
      p.motion[location][0] = _Float16(std::numeric_limits<float>::quiet_NaN());
    }, false);
    run(width, height, "nonfinite motion Y", [=](Pixels& p) {
      p.motion[location][1] = _Float16(-std::numeric_limits<float>::infinity());
    }, false);
    run(width, height, "invalid current depth", [=](Pixels& p) { p.depth[location] = -1; }, false);
    run(width, height, "nonfinite current depth", [=](Pixels& p) {
      p.depth[location] = std::numeric_limits<float>::infinity();
    }, false);
    run(width, height, "nonfinite previous depth", [=](Pixels& p) {
      p.previous[location] = std::numeric_limits<float>::quiet_NaN();
    }, false);
    run(width, height, "unavailable previous surface", [=](Pixels& p) { p.previous[location] = -1; }, false);
    run(width, height, "previous depth beyond far plane", [=](Pixels& p) { p.previous[location] = 1.01f; }, false);
  }
  run(width, height, "finite bounds and stationary zero", [](Pixels& p) {
    p.motion.front() = {_Float16(65504), _Float16(-65504)};
    p.depth.front() = p.previous.front() = 0;
    p.depth.back() = p.previous.back() = 1;
  }, true);
  run(width, height, "all pixels incomplete", [](Pixels& p) {
    std::fill(p.reactive.begin(), p.reactive.end(), uint8_t(255));
    std::fill(p.previous.begin(), p.previous.end(), -1.0f);
  }, false);

  // Inputs written by earlier GPU encoders must be observed, not a CPU copy.
  {
    auto in = Upload(device, Pixels(width, height), test);
    auto cb = [queue commandBuffer];
    auto pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = in.previous_depth;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(-1, 0, 0, 0);
    auto render = [cb renderCommandEncoderWithDescriptor:pass];
    test.Require(render != nil, "GPU reactive-mask producer");
    [render endEncoding];
    test.Require(coverage.Encode(cb, in, library, error), error);
    Submit(cb, test);
    test.Require(!coverage.Complete(), "ignored the current frame's GPU geometric invalidation");
    ++test.cases;
  }

  // Encode two submissions before either commits. The second result must not
  // reuse storage that the first submission can still modify.
  {
    Pixels bad(width, height), good(width, height);
    bad.previous.back() = -1;
    auto first = [queue commandBuffer], second = [queue commandBuffer];
    const auto first_in = Upload(device, bad, test), second_in = Upload(device, good, test);
    test.Require(coverage.Encode(first, first_in, library, error), error);
    test.Require(coverage.Encode(second, second_in, library, error), error);
    test.Require(!coverage.Complete(), "accepted a pending second frame");
    Submit(first, test);
    test.Require(!coverage.Complete(), "the first completion certified the second frame");
    Submit(second, test);
    test.Require(coverage.Complete(), "pending result storage was overwritten or reused");
    ++test.cases;
  }

  // Failed encodes also invalidate any prior successful result.
  {
    auto in = Upload(device, Pixels(width, height), test);
    auto cb = [queue commandBuffer];
    auto missing = in;
    missing.previous_depth = nil;
    test.Require(!coverage.Encode(cb, missing, library, error), "accepted missing previous depth");
    test.Require(!coverage.Complete(), "failed encode retained previous admission");
    auto mismatch = in;
    mismatch.motion = Image(device, MTLPixelFormatRG16Float, 1, 1, test);
    test.Require(!coverage.Encode(cb, mismatch, library, error), "accepted mismatched extents");
    auto wrong_format = in;
    wrong_format.reactive = Image(device, MTLPixelFormatR32Float, width, height, test);
    test.Require(!coverage.Encode(cb, wrong_format, library, error), "accepted wrong reactive format");
    test.Require(!coverage.Encode(nil, in, library, error), "accepted nil command buffer");
    test.Require(!coverage.Encode(cb, in, nil, error), "accepted nil shader library");
    test.Require(coverage.Encode(cb, in, library, error), error);
    Submit(cb, test);
    test.Require(coverage.Complete(), "valid input after rejection was not admitted");
    test.Require(!coverage.Encode(cb, in, library, error), "accepted an already committed buffer");
    test.Require(!coverage.Complete(), "rejected recording kept old success");
    ++test.cases;
  }
  std::printf("metal_temporal_motion_coverage=passed cases=%zu checks=%zu\n", test.cases, test.checks);
}
