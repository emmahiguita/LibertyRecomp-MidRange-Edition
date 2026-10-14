// Isolated differential of the complete stock and repaired composite libraries.
// Usage: probe stock.metallib repaired.metallib constants-dir [max-half-ulp] [temporal] [--split]
// The directory contains probe-{vertex,pixel,shared}.bin from the retained capture.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr size_t kWidth = 4, kHeight = 4, kChannels = 64;
constexpr size_t kGammaIndex = 842, kSaturationIndex = 840, kExposureIndex = 832;
constexpr size_t kCorrectionIndex = 844;
constexpr size_t kSplitModeByteOffset = 596;
constexpr size_t kHighWidth = 8, kHighHeight = 8;
using Pixels = std::array<_Float16, kChannels>;

// Independent bilinear reference for the enlarged-output test. Metal's
// normalized sampling addresses texel centers; clamp each source tap, not the
// continuous coordinate, so the border agrees with clamp-to-edge sampling.
std::vector<float> ResizeReference(const std::vector<float>& source,
                                  size_t width, size_t height,
                                  size_t output_width, size_t output_height) {
  std::vector<float> output(output_width * output_height * 4);
  for (size_t y = 0; y < output_height; ++y) {
    const float sy = (float(y) + 0.5f) * float(height) / float(output_height) - 0.5f;
    const int iy = int(std::floor(sy));
    const float fy = sy - float(iy);
    for (size_t x = 0; x < output_width; ++x) {
      const float sx = (float(x) + 0.5f) * float(width) / float(output_width) - 0.5f;
      const int ix = int(std::floor(sx));
      const float fx = sx - float(ix);
      for (size_t c = 0; c < 4; ++c) {
        auto tap = [&](int tx, int ty) {
          const size_t px = size_t(std::clamp(tx, 0, int(width) - 1));
          const size_t py = size_t(std::clamp(ty, 0, int(height) - 1));
          return source[(py * width + px) * 4 + c];
        };
        const float top = tap(ix, iy) * (1 - fx) + tap(ix + 1, iy) * fx;
        const float bottom = tap(ix, iy + 1) * (1 - fx) + tap(ix + 1, iy + 1) * fx;
        output[(y * output_width + x) * 4 + c] = top * (1 - fy) + bottom * fy;
      }
    }
  }
  return output;
}

void Check(bool value, NSString* reason) {
  if (!value) throw std::runtime_error(reason ? reason.UTF8String : "Metal failure");
}

uint32_t HalfOrder(_Float16 value) {
  uint16_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  const uint32_t magnitude = bits & 0x7fffu;
  return (bits & 0x8000u) ? 0x8000u - magnitude : 0x8000u + magnitude;
}

struct Case {
  std::string name;
  float gamma, level;
  uint32_t pattern = 0;
  bool black = false, modify_color = false;
};

struct TemporalParameters {
  float input_extent[2] = {float(kWidth), float(kHeight)};
  float current_jitter[2]{}, previous_jitter[2]{};
  uint32_t valid_history = 0, reactive = 0, ui_mode = 0, padding[3]{};
};
static_assert(sizeof(TemporalParameters) == 48);
}  // namespace

int main(int argc, char** argv) {
  @autoreleasepool {
    try {
      Check(argc >= 4 && argc <= 7,
            @"expected stock.metallib repaired.metallib constants-dir [max-half-ulp] [temporal] [--split]");
      unsigned allowed_ulp = 0;
      bool temporal = false, split = false, have_ulp = false;
      for (int i = 4; i < argc; ++i) {
        if (std::strcmp(argv[i], "temporal") == 0) temporal = true;
        else if (std::strcmp(argv[i], "--split") == 0) split = true;
        else {
          size_t consumed = 0;
          const auto value = std::stoul(argv[i], &consumed);
          Check(!have_ulp && consumed == std::strlen(argv[i]) && value <= UINT32_MAX,
                @"unknown probe option");
          allowed_ulp = unsigned(value);
          have_ulp = true;
        }
      }
      const std::array<MTLPixelFormat, 4> auxiliary_formats{
        MTLPixelFormatRG16Float, MTLPixelFormatR8Unorm,
        MTLPixelFormatRGBA16Float, MTLPixelFormatRGBA16Float};
      auto device = MTLCreateSystemDefaultDevice();
      Check(device != nil, @"GPU unavailable");
      auto queue = [device newCommandQueue];
      Check(queue != nil, @"command queue unavailable");
      NSError* error = nil;
      NSString* directory = [NSString stringWithUTF8String:argv[3]];
      auto load = [&](NSString* filename) {
        auto data = [NSData dataWithContentsOfFile:[directory stringByAppendingPathComponent:filename]];
        Check(data != nil, @"missing captured constant bank");
        auto result = [device newBufferWithBytes:data.bytes length:data.length
                                        options:MTLResourceStorageModeShared];
        Check(result != nil, @"constant buffer allocation failed");
        return result;
      };
      auto vertex = load(@"probe-vertex.bin");
      auto pixel = load(@"probe-pixel.bin");
      auto shared = load(@"probe-shared.bin");
      Check(pixel.length >= (kCorrectionIndex + 4) * sizeof(float), @"pixel constants too short");
      Check(shared.length >= kSplitModeByteOffset + sizeof(float), @"shared constants too short");
      std::vector<uint8_t> original(pixel.length);
      std::memcpy(original.data(), pixel.contents, original.size());
      const float recorded_gamma = static_cast<const float*>(pixel.contents)[kGammaIndex];
      Check(recorded_gamma > 0 && recorded_gamma < 1, @"capture lacks the singular positive gamma");

      NSString* vertex_source = [NSString stringWithFormat:@"#include <metal_stdlib>\nusing namespace metal;"
        "struct Out { float4 p [[position]]; float4 uv [[user(TEXCOORD0)]];"
        "float4 auxiliary [[user(TEXCOORD1)]];"
        "float4 current [[user(LIBERTY_CURRENT_CLIP)]];"
        "float4 previous [[user(LIBERTY_PREVIOUS_CLIP)]]; };"
        "vertex Out vs(uint i [[vertex_id]]) {"
        "float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};"
        "float2 uv=(p[i]+1)*0.5;if(%@)uv.y=1-uv.y;"
        "return {float4(p[i],0,1),float4(uv,0,0),float4(0),"
        "float4(p[i],0,1),float4(p[i],0,1)}; }", split ? @"true" : @"false"];
      auto vertex_library = [device newLibraryWithSource:vertex_source options:nil error:&error];
      Check(vertex_library != nil, error.description);
      auto make_pipeline = [&](const char* path, MTLPixelFormat format) {
        auto url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
        auto library = [device newLibraryWithURL:url error:&error];
        Check(library != nil, error.description);
        auto constants = [MTLFunctionConstantValues new];
        uint32_t specialization = 0;
        [constants setConstantValue:&specialization type:MTLDataTypeUInt atIndex:0];
        auto fragment = [library newFunctionWithName:@"shaderMain" constantValues:constants error:&error];
        Check(fragment != nil, error.description);
        auto descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = [vertex_library newFunctionWithName:@"vs"];
        descriptor.fragmentFunction = fragment;
        descriptor.colorAttachments[0].pixelFormat = format;
        if (temporal)
          for (size_t i = 0; i < auxiliary_formats.size(); ++i)
            descriptor.colorAttachments[i + 4].pixelFormat = auxiliary_formats[i];
        auto result = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        Check(result != nil, error.description);
        return result;
      };
      auto stock = make_pipeline(argv[1], MTLPixelFormatRGBA16Float);
      auto repaired = make_pipeline(argv[2], MTLPixelFormatRGBA16Float);
      id<MTLRenderPipelineState> filtered_pipeline = split
          ? make_pipeline(argv[2], MTLPixelFormatRGBA32Float) : nil;
      auto texture = [&](MTLPixelFormat format, size_t width = kWidth,
                         size_t height = kHeight) {
        auto descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
          width:width height:height mipmapped:NO];
        descriptor.storageMode = MTLStorageModeShared;
        descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
        auto result = [device newTextureWithDescriptor:descriptor];
        Check(result != nil, @"texture allocation failed");
        return result;
      };
      auto gbuffer = texture(MTLPixelFormatRGBA8Unorm);
      auto hdr = texture(MTLPixelFormatRGBA16Float);
      auto adaptation = texture(MTLPixelFormatR32Float, split ? 1 : kWidth,
                               split ? 1 : kHeight);
      auto target = texture(MTLPixelFormatRGBA16Float);
      std::array<id<MTLTexture>, 4> ordinary_auxiliary{};
      if (temporal)
        for (size_t i = 0; i < ordinary_auxiliary.size(); ++i)
          ordinary_auxiliary[i] = texture(auxiliary_formats[i]);
      std::array<uint8_t, kChannels> gbuffer_data{};
      [gbuffer replaceRegion:MTLRegionMake2D(0, 0, kWidth, kHeight) mipmapLevel:0
                   withBytes:gbuffer_data.data() bytesPerRow:16];
      std::array<float, 16> adaptation_data;
      adaptation_data.fill(19.88f);
      [adaptation replaceRegion:MTLRegionMake2D(0, 0, adaptation.width, adaptation.height) mipmapLevel:0
                      withBytes:adaptation_data.data() bytesPerRow:adaptation.width * sizeof(float)];
      auto sampler_descriptor = [MTLSamplerDescriptor new];
      sampler_descriptor.supportArgumentBuffers = YES;
      sampler_descriptor.minFilter = sampler_descriptor.magFilter = MTLSamplerMinMagFilterLinear;
      sampler_descriptor.sAddressMode = sampler_descriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
      auto sampler = [device newSamplerStateWithDescriptor:sampler_descriptor];
      Check(sampler != nil, @"sampler allocation failed");
      std::array<std::array<uint64_t, 26>, 5> heaps{};
      for (auto& heap : heaps) heap.fill(gbuffer.gpuResourceID._impl);
      heaps[0][0] = gbuffer.gpuResourceID._impl;
      heaps[0][1] = hdr.gpuResourceID._impl;
      heaps[0][2] = adaptation.gpuResourceID._impl;
      heaps[4].fill(sampler.gpuResourceID._impl);
      auto arguments = [device newBufferWithBytes:heaps.data() length:sizeof(heaps)
                                         options:MTLResourceStorageModeShared];
      Check(arguments != nil, @"argument buffer allocation failed");
      const std::array<uint64_t, 3> push{vertex.gpuAddress, pixel.gpuAddress, shared.gpuAddress};
      auto draw_to = [&](id<MTLRenderPipelineState> pipeline, id<MTLTexture> scene,
                         id<MTLTexture> destination, float mode) {
        Check(scene != destination, @"probe input aliases its attachment");
        std::memcpy(static_cast<uint8_t*>(shared.contents) + kSplitModeByteOffset,
                    &mode, sizeof(mode));
        heaps[0][1] = scene.gpuResourceID._impl;
        std::memcpy(arguments.contents, heaps.data(), sizeof(heaps));
        auto auxiliary = ordinary_auxiliary;
        if (temporal && (destination.width != kWidth || destination.height != kHeight))
          for (size_t i = 0; i < auxiliary.size(); ++i)
            auxiliary[i] = texture(auxiliary_formats[i], destination.width, destination.height);
        TemporalParameters temporal_parameters{};
        temporal_parameters.input_extent[0] = float(destination.width);
        temporal_parameters.input_extent[1] = float(destination.height);
        auto command = [queue commandBuffer];
        auto pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = destination;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        if (temporal)
          for (size_t i = 0; i < auxiliary.size(); ++i) {
            pass.colorAttachments[i + 4].texture = auxiliary[i];
            pass.colorAttachments[i + 4].loadAction = MTLLoadActionClear;
            pass.colorAttachments[i + 4].storeAction = MTLStoreActionStore;
          }
        auto encoder = [command renderCommandEncoderWithDescriptor:pass];
        Check(encoder != nil, @"render encoder unavailable");
        [encoder setRenderPipelineState:pipeline];
        for (NSUInteger i = 0; i < heaps.size(); ++i)
          [encoder setFragmentBuffer:arguments offset:i * sizeof(heaps[0]) atIndex:i];
        [encoder setFragmentBytes:push.data() length:sizeof(push) atIndex:8];
        if (temporal)
          [encoder setFragmentBytes:&temporal_parameters length:sizeof(temporal_parameters) atIndex:6];
        const std::array<id<MTLResource>, 6> resources{vertex, pixel, shared, gbuffer, scene, adaptation};
        [encoder useResources:resources.data() count:resources.size()
                        usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [encoder endEncoding];
        [command commit];
        [command waitUntilCompleted];
        Check(command.status == MTLCommandBufferStatusCompleted, command.error.description);
      };
      auto read_half = [&](id<MTLTexture> image) {
        std::vector<_Float16> result(image.width * image.height * 4);
        [image getBytes:result.data() bytesPerRow:image.width * 4 * sizeof(_Float16)
             fromRegion:MTLRegionMake2D(0, 0, image.width, image.height) mipmapLevel:0];
        return result;
      };
      auto read_float = [&](id<MTLTexture> image) {
        std::vector<float> result(image.width * image.height * 4);
        [image getBytes:result.data() bytesPerRow:image.width * 4 * sizeof(float)
             fromRegion:MTLRegionMake2D(0, 0, image.width, image.height) mipmapLevel:0];
        return result;
      };
      auto draw = [&](id<MTLRenderPipelineState> pipeline) {
        draw_to(pipeline, hdr, target, 0);
        Pixels result;
        [target getBytes:result.data() bytesPerRow:32
              fromRegion:MTLRegionMake2D(0, 0, kWidth, kHeight) mipmapLevel:0];
        return result;
      };

      std::vector<Case> cases;
      for (float gamma : {recorded_gamma, 0.5f, 1.0f, 2.2f}) {
        cases.push_back({"black", gamma, 0, 0, true});
        for (float value : {0.0001f, 0.01f, 0.1f, 1.0f, 16.0f})
          cases.push_back({"uniform", gamma, value});
        cases.push_back({"gradient", gamma, 0.25f, 1});
        cases.push_back({"colored-HDR", gamma, 8.0f, 2});
        cases.push_back({"color-correction", gamma, 0.1f, 1, false, true});
      }
      size_t reproduced_nan_channels = 0, finite_compared = 0, changed_finite = 0;
      uint32_t maximum_ulp = 0;
      bool passed = true;
      for (const auto& test : cases) {
        std::memcpy(pixel.contents, original.data(), original.size());
        auto* constants = static_cast<float*>(pixel.contents);
        constants[kGammaIndex] = test.gamma;
        if (test.modify_color) {
          constants[kSaturationIndex] = 0.35f;
          constants[kExposureIndex] = 1.5f;
          constants[kCorrectionIndex] = 0.3f;
          constants[kCorrectionIndex + 1] = 0.7f;
          constants[kCorrectionIndex + 2] = 0.9f;
        }
        Pixels input{};
        for (size_t p = 0; p < kWidth * kHeight; ++p) {
          float factor = test.pattern == 1 ? float(p + 1) / float(kWidth * kHeight) : 1.0f;
          input[p * 4] = _Float16(test.level * factor);
          input[p * 4 + 1] = _Float16(test.level * factor * (test.pattern == 2 ? 0.25f : 1.0f));
          input[p * 4 + 2] = _Float16(test.level * factor * (test.pattern == 2 ? 2.0f : 1.0f));
          input[p * 4 + 3] = _Float16(1);
        }
        [hdr replaceRegion:MTLRegionMake2D(0, 0, kWidth, kHeight) mipmapLevel:0
                   withBytes:input.data() bytesPerRow:32];
        auto before = draw(stock), after = draw(repaired);
        size_t stock_nonfinite = 0, fixed_nonfinite = 0, differences = 0;
        for (size_t i = 0; i < kChannels; ++i) {
          bool finite_before = std::isfinite(float(before[i]));
          bool finite_after = std::isfinite(float(after[i]));
          stock_nonfinite += !finite_before;
          fixed_nonfinite += !finite_after;
          if (test.black) {
            if (i % 4 != 3) {
              reproduced_nan_channels += std::isnan(float(before[i]));
              passed &= finite_after && float(after[i]) == 0;
            } else {
              passed &= float(after[i]) == 1;
            }
          } else {
            // All nonblack fixtures intentionally have positive luminance.
            passed &= finite_before && finite_after;
          }
          if (finite_before && finite_after) {
            ++finite_compared;
            const uint32_t a = HalfOrder(before[i]), b = HalfOrder(after[i]);
            const uint32_t distance = a > b ? a - b : b - a;
            maximum_ulp = distance > maximum_ulp ? distance : maximum_ulp;
            differences += distance != 0;
            changed_finite += distance != 0;
            passed &= distance <= allowed_ulp;
          }
        }
        std::printf("case=%s gamma=%.9g input=%.9g stock_nonfinite=%zu repaired_nonfinite=%zu finite_differences=%zu\n",
                    test.name.c_str(), test.gamma, test.level, stock_nonfinite, fixed_nonfinite, differences);
      }
      // The negative control must reproduce the defect; otherwise this is not
      // testing the captured zero-luminance path or the supplied stock library.
      passed &= reproduced_nan_channels != 0;
      if (split) {
        struct SplitCase {
          std::string name;
          uint8_t selector;
          float gamma;
          bool gradient = false, mixed_selectors = false, black = false;
        };
        std::vector<SplitCase> split_cases;
        // The producer encodes four neighbor choices into values 0..15.
        // 255 also exercises the stock decoder's final threshold at alpha=1.
        for (uint8_t selector = 0; selector < 16; ++selector)
          split_cases.push_back({"asymmetric", selector, recorded_gamma});
        split_cases.push_back({"asymmetric", 255, recorded_gamma});
        split_cases.push_back({"gradient", 0, recorded_gamma, true});
        split_cases.push_back({"mixed-selectors-color", 0, recorded_gamma, false, true});
        for (float gamma : {recorded_gamma, 0.5f, 1.0f, 2.2f})
          split_cases.push_back({"black", 0, gamma, false, false, true});
        constexpr std::array<float, 16> asymmetric{
          0.125f, 3.625f, 0.375f, 1.375f, 0.875f, 0.25f, 5.375f, 0.625f,
          2.375f, 7.625f, 1.625f, 2.125f, 4.625f, 2.875f, 5.875f, 3.875f};
        auto filtered = texture(MTLPixelFormatRGBA32Float);
        auto split_target = texture(MTLPixelFormatRGBA16Float);
        auto high_target = texture(MTLPixelFormatRGBA16Float, kHighWidth, kHighHeight);
        auto high_reference = texture(MTLPixelFormatRGBA16Float, kHighWidth, kHighHeight);
        auto high_input = texture(MTLPixelFormatRGBA32Float, kHighWidth, kHighHeight);
        auto wrong_target = texture(MTLPixelFormatRGBA16Float, kHighWidth, kHighHeight);
        size_t native_compared = 0, high_compared = 0, high_cases = 0;
        size_t doubled_filter_differences = 0, output_grid_differences = 0;
        uint32_t native_maximum_ulp = 0, high_maximum_ulp = 0;
        // Native split has no FP16 intermediate and uses the caller's original
        // tolerance. Only the independent CPU-vs-GPU bilinear enlargement may
        // differ by one final half ULP due to FP32 interpolation rounding.
        const uint32_t high_allowed_ulp = std::max(allowed_ulp, 1u);
        auto compare = [&](const SplitCase& test, const char* stage,
                           const std::vector<_Float16>& expected,
                           const std::vector<_Float16>& actual, size_t width,
                           uint32_t tolerance, size_t& compared, uint32_t& maximum) {
          Check(expected.size() == actual.size(), @"split image extents disagree");
          size_t failures = 0;
          for (size_t i = 0; i < expected.size(); ++i) {
            const bool finite = std::isfinite(float(expected[i])) && std::isfinite(float(actual[i]));
            const uint32_t a = HalfOrder(expected[i]), b = HalfOrder(actual[i]);
            const uint32_t distance = a > b ? a - b : b - a;
            maximum = std::max(maximum, distance);
            ++compared;
            if (!finite || distance > tolerance) {
              passed = false;
              if (failures++ < 4)
                std::fprintf(stderr,
                    "split-mismatch case=%s selector=%u gamma=%.9g stage=%s x=%zu y=%zu channel=%zu expected=%.9g actual=%.9g half-ulp=%u allowed=%u\n",
                    test.name.c_str(), unsigned(test.selector), test.gamma, stage,
                    (i / 4) % width, (i / 4) / width, i % 4,
                    float(expected[i]), float(actual[i]), distance, tolerance);
            }
          }
          return failures;
        };
        auto differences = [&](const std::vector<_Float16>& a,
                               const std::vector<_Float16>& b) {
          Check(a.size() == b.size(), @"negative-control extents disagree");
          size_t result = 0;
          for (size_t i = 0; i < a.size(); ++i) {
            Check(std::isfinite(float(a[i])) && std::isfinite(float(b[i])),
                  @"negative control produced nonfinite color");
            const uint32_t x = HalfOrder(a[i]), y = HalfOrder(b[i]);
            const uint32_t distance = x > y ? x - y : y - x;
            result += distance > high_allowed_ulp;
          }
          return result;
        };
        for (const auto& test : split_cases) {
          std::memcpy(pixel.contents, original.data(), original.size());
          auto* constants = static_cast<float*>(pixel.contents);
          constants[kGammaIndex] = test.gamma;
          if (test.mixed_selectors) {
            constants[kSaturationIndex] = 0.35f;
            constants[kExposureIndex] = 1.5f;
            constants[kCorrectionIndex] = 0.3f;
            constants[kCorrectionIndex + 1] = 0.7f;
            constants[kCorrectionIndex + 2] = 0.9f;
          }
          Pixels input{};
          for (size_t p = 0; p < kWidth * kHeight; ++p) {
            const float value = test.black ? 0 : test.gradient
                ? float(p + 1) / 8.0f : asymmetric[p];
            input[p * 4] = _Float16(value);
            input[p * 4 + 1] = _Float16(test.black ? 0 : value * 0.25f + 0.125f);
            input[p * 4 + 2] = _Float16(test.black ? 0 :
                (test.gradient ? value * 0.5f : asymmetric[asymmetric.size() - 1 - p]));
            input[p * 4 + 3] = _Float16(1);
            gbuffer_data[p * 4 + 3] = test.mixed_selectors ? uint8_t(p) : test.selector;
          }
          [gbuffer replaceRegion:MTLRegionMake2D(0, 0, kWidth, kHeight) mipmapLevel:0
                       withBytes:gbuffer_data.data() bytesPerRow:kWidth * 4];
          [hdr replaceRegion:MTLRegionMake2D(0, 0, kWidth, kHeight) mipmapLevel:0
                   withBytes:input.data() bytesPerRow:kWidth * 4 * sizeof(_Float16)];
          draw_to(repaired, hdr, target, 0);
          const auto unsplit = read_half(target);
          draw_to(filtered_pipeline, hdr, filtered, 1);
          const auto linear = read_float(filtered);
          for (size_t i = 0; i < linear.size(); ++i) {
            Check(std::isfinite(linear[i]), @"stock filter scratch contains nonfinite color");
            if (i % 4 == 3) Check(linear[i] == 1.0f, @"stock filter scratch alpha changed");
          }
          draw_to(repaired, filtered, split_target, 2);
          const auto reconstructed = read_half(split_target);
          const size_t failures = compare(test, "native-filter-then-tone", unsplit, reconstructed,
              kWidth, allowed_ulp, native_compared, native_maximum_ulp);
          std::printf("split-case=%s selector=%u gamma=%.9g failures=%zu\n",
                      test.name.c_str(), unsigned(test.selector), test.gamma, failures);
          if (test.name != "asymmetric" || (test.selector != 0 && test.selector != 15)) continue;
          ++high_cases;
          // Correct path: filter once at the original extent, enlarge the
          // linear result, then run only the stock tone/color tail.
          draw_to(repaired, filtered, high_target, 2);
          const auto high = read_half(high_target);
          const auto enlarged = ResizeReference(linear, kWidth, kHeight, kHighWidth, kHighHeight);
          [high_input replaceRegion:MTLRegionMake2D(0, 0, kHighWidth, kHighHeight) mipmapLevel:0
                          withBytes:enlarged.data() bytesPerRow:kHighWidth * 4 * sizeof(float)];
          draw_to(repaired, high_input, high_reference, 2);
          compare(test, "enlarged-filter-then-tone", read_half(high_reference), high,
                  kHighWidth, high_allowed_ulp, high_compared, high_maximum_ulp);
          // Negative control 1: mode 0 filters the already-filtered image again.
          draw_to(repaired, filtered, wrong_target, 0);
          const size_t doubled = differences(high, read_half(wrong_target));
          doubled_filter_differences += doubled;
          // Negative control 2: enlarge raw HDR first, moving the original
          // filter's pixel offsets to the enlarged grid instead of preserving
          // its input-grid footprint.
          const std::vector<float> raw(input.begin(), input.end());
          const auto raw_enlarged = ResizeReference(raw, kWidth, kHeight, kHighWidth, kHighHeight);
          [high_input replaceRegion:MTLRegionMake2D(0, 0, kHighWidth, kHighHeight) mipmapLevel:0
                          withBytes:raw_enlarged.data() bytesPerRow:kHighWidth * 4 * sizeof(float)];
          draw_to(repaired, high_input, wrong_target, 0);
          const size_t changed_grid = differences(high, read_half(wrong_target));
          output_grid_differences += changed_grid;
          passed &= doubled != 0 && changed_grid != 0;
          std::printf("split-high selector=%u doubled-filter-differences=%zu changed-grid-differences=%zu\n",
                      unsigned(test.selector), doubled, changed_grid);
        }
        std::printf("{\"split_cases\":%zu,\"split_native_channels\":%zu,\"split_native_max_half_ulp\":%u,"
                    "\"split_high_cases\":%zu,\"split_high_channels\":%zu,\"split_high_max_half_ulp\":%u,"
                    "\"split_high_allowed_half_ulp\":%u,\"doubled_filter_differences\":%zu,"
                    "\"changed_filter_grid_differences\":%zu}\n",
                    split_cases.size(), native_compared, native_maximum_ulp, high_cases,
                    high_compared, high_maximum_ulp, high_allowed_ulp,
                    doubled_filter_differences, output_grid_differences);
      }
      std::printf("{\"passed\":%s,\"temporal\":%s,\"cases\":%zu,\"stock_nan_channels_reproduced\":%zu,"
                  "\"finite_channels_compared\":%zu,\"finite_channels_changed\":%zu,"
                  "\"maximum_half_ulp\":%u,\"allowed_half_ulp\":%u}\n",
                  passed ? "true" : "false", temporal ? "true" : "false", cases.size(), reproduced_nan_channels,
                  finite_compared, changed_finite, maximum_ulp, allowed_ulp);
      return passed ? 0 : 1;
    } catch (const std::exception& error) {
      std::fprintf(stderr, "%s\n", error.what());
      return 1;
    }
  }
}
