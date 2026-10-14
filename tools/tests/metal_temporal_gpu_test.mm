#include "graphics/gta4_metal/temporal/effects.h"
#include "graphics/gta4_metal/temporal/geometry_history.h"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
using namespace rex::graphics::gta4_metal::temporal;
void TestMotionCoverage(id<MTLDevice>, id<MTLCommandQueue>, id<MTLLibrary>);
namespace {
size_t checks = 0, cases = 0;
void Check(bool value, const std::string &message) {
  ++checks;
  if (!value)
    throw std::runtime_error(message);
}
using Pixel = std::array<float, 4>;
struct Fixture {
  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  id<MTLCommandQueue> queue = [device newCommandQueue];
  id<MTLLibrary> library = nil;
  explicit Fixture(const char *path) {
    NSError *e = nil;
    library = [device newLibraryWithURL:[NSURL fileURLWithPath:@(path)]
                                  error:&e];
    Check(library != nil, e ? e.localizedDescription.UTF8String
                            : "missing temporal shader library");
  }
  id<MTLTexture> Image(MTLPixelFormat format, uint32_t width, uint32_t height) {
    auto d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                                width:width
                                                               height:height
                                                            mipmapped:NO];
    d.storageMode = MTLStorageModeShared;
    d.usage = MTLTextureUsageShaderRead;
    d.hazardTrackingMode = MTLHazardTrackingModeTracked;
    auto image = [device newTextureWithDescriptor:d];
    Check(image != nil, "input image allocation failed");
    return image;
  }
  Inputs Textures(uint32_t w, uint32_t h) {
    return {Image(MTLPixelFormatRGBA16Float, w, h),
            Image(MTLPixelFormatR32Float, w, h),
            Image(MTLPixelFormatRG16Float, w, h),
            Image(MTLPixelFormatR8Unorm, w, h),
            Image(MTLPixelFormatR32Float, w, h)};
  }
  void WriteColor(id<MTLTexture> image, const std::vector<Pixel> &pixels) {
    Check(pixels.size() == image.width * image.height, "color fixture extent");
    std::vector<_Float16> bytes(pixels.size() * 4);
    for (size_t i = 0; i < pixels.size(); ++i)
      for (size_t c = 0; c < 4; ++c)
        bytes[i * 4 + c] = _Float16(pixels[i][c]);
    [image replaceRegion:MTLRegionMake2D(0, 0, image.width, image.height)
             mipmapLevel:0
               withBytes:bytes.data()
             bytesPerRow:image.width * 4 * sizeof(_Float16)];
  }
  void WriteDepth(id<MTLTexture> image, const std::vector<float> &values) {
    Check(values.size() == image.width * image.height, "depth fixture extent");
    [image replaceRegion:MTLRegionMake2D(0, 0, image.width, image.height)
             mipmapLevel:0
               withBytes:values.data()
             bytesPerRow:image.width * sizeof(float)];
  }
  void WriteMotion(id<MTLTexture> image,
                   const std::vector<std::array<float, 2>> &values) {
    Check(values.size() == image.width * image.height, "motion fixture extent");
    std::vector<_Float16> v(values.size() * 2);
    for (size_t i = 0; i < values.size(); ++i) {
      v[2 * i] = _Float16(values[i][0]);
      v[2 * i + 1] = _Float16(values[i][1]);
    }
    [image replaceRegion:MTLRegionMake2D(0, 0, image.width, image.height)
             mipmapLevel:0
               withBytes:v.data()
             bytesPerRow:image.width * 2 * sizeof(_Float16)];
  }
  void Reactive(id<MTLTexture> image, uint8_t value) {
    std::vector<uint8_t> bytes(image.width * image.height, value);
    [image replaceRegion:MTLRegionMake2D(0, 0, image.width, image.height)
             mipmapLevel:0
               withBytes:bytes.data()
             bytesPerRow:image.width];
  }
  void Uniform(const Inputs &in, Pixel color, float depth = 0.5f) {
    const size_t n = in.color.width * in.color.height;
    WriteColor(in.color, std::vector<Pixel>(n, color));
    WriteDepth(in.depth, std::vector<float>(n, depth));
    WriteDepth(in.previous_depth, std::vector<float>(n, depth));
    WriteMotion(in.motion, std::vector<std::array<float, 2>>(n, {0, 0}));
    Reactive(in.reactive, 0);
  }
  std::vector<Pixel> Read(id<MTLCommandBuffer> commands, id<MTLTexture> image) {
    Check(image != nil, "missing output image");
    const size_t pitch = (image.width * 8 + 255) & ~size_t(255);
    auto bytes = [device newBufferWithLength:pitch * image.height
                                     options:MTLResourceStorageModeShared];
    auto blit = [commands blitCommandEncoder];
    Check(bytes && blit, "readback allocation");
    [blit copyFromTexture:image
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(image.width, image.height, 1)
                        toBuffer:bytes
               destinationOffset:0
          destinationBytesPerRow:pitch
        destinationBytesPerImage:pitch * image.height];
    [blit endEncoding];
    [commands commit];
    [commands waitUntilCompleted];
    Check(commands.status == MTLCommandBufferStatusCompleted,
          commands.error ? commands.error.localizedDescription.UTF8String
                         : "temporal GPU failure");
    std::vector<Pixel> pixels(image.width * image.height);
    for (size_t y = 0; y < image.height; ++y)
      for (size_t x = 0; x < image.width; ++x)
        for (size_t c = 0; c < 4; ++c) {
          _Float16 v;
          std::memcpy(&v,
                      static_cast<const uint8_t *>(bytes.contents) + y * pitch +
                          x * 8 + c * 2,
                      2);
          pixels[y * image.width + x][c] = float(v);
          Check(std::isfinite(float(v)), "nonfinite temporal output");
        }
    ++cases;
    return pixels;
  }
};
Frame MakeFrame(uint64_t sequence) {
  Frame f;
  f.sequence = sequence;
  f.epoch = 1;
  f.view = 1;
  f.time_ns = 1000000000ull + sequence * 16666667ull;
  f.coverage = InputCoverage::kSceneMotion;
  f.color_domain = ColorDomain::kSceneLinear;
  f.scene_without_ui = f.jitter_applied = true;
  return f;
}
void LargeHistoryAllocation(Fixture& f) {
  AntiAliasing aa;
  std::string error;
  const auto budget = OwnedTextureBudget(f.device);
  Configuration c{3840,2160,3840,2160,Method::kTaa,false};
  Check(!aa.Configure(f.device,f.library,c,256u*1024u*1024u,error),
        "4K history silently exceeded the old budget");
  const auto rejected = error;
  aa.Reset();
  Check(!aa.Configure(f.device,f.library,c,256u*1024u*1024u,error) &&
            error == rejected && aa.owned_bytes() == 0,
        "failed 4K configuration retained partial history");
  Check(aa.Configure(f.device,f.library,c,budget,error),error);
  Check(aa.owned_bytes() >= 298598400 && aa.owned_bytes() <= budget,
        "4K history allocation was incomplete or exceeded the device budget");
  c = {4096,4096,4096,4096,Method::kTaa,false};
  Check(aa.Configure(f.device,f.library,c,budget,error),error);
  Check(aa.owned_bytes() >= 603979776 && aa.owned_bytes() <= budget,
        "largest live extent has incomplete native history");
  c = {64,64,64,64,Method::kTaa,false};
  Check(aa.Configure(f.device,f.library,c,budget,error),error);
  std::printf("native_taa_4k_and_max_extent_history=passed budget=%zu\n",budget);
}

void GeometryIdentityTests() {
  std::array<uint8_t, 4096> vertices{};
  std::array<uint8_t, 1024> shared{};
  GeometryKey a{1, 2, 3, 4, 5, 6, 7}, b = a;
  b.instance = 2;
  GeometryHistory history(12 * 1024, 8);
  Check(history.Begin(1, 1, 1, false), "geometry first frame");
  vertices[0] = 10;
  Check(history.Record(a, vertices, shared, {}).captured,
        "first geometry snapshot");
  vertices[0] = 20;
  Check(history.Record(b, vertices, shared, {}).captured,
        "second instance snapshot");
  Check(history.owned_bytes() == 2 * (4096 + 1024),
        "history budget accounting");
  Check(history.Begin(1, 1, 2, false), "geometry next frame");
  vertices[0] = 11;
  auto pressure = history.Record(a, vertices, shared, {});
  Check(!pressure.captured && !pressure.previous,
        "capture ignored total budget");
  history.Reset();
  Check(history.owned_bytes() == 0, "geometry reset budget");
  GeometryHistory normal(64 * 1024, 8);
  Check(normal.Begin(1, 1, 1, false), "normal geometry first frame");
  vertices[0] = 10;
  normal.Record(a, vertices, shared, {});
  vertices[0] = 20;
  normal.Record(b, vertices, shared, {});
  Check(normal.Begin(1, 1, 2, false), "geometry continuity");
  vertices[0] = 21;
  auto second = normal.Record(b, vertices, shared, {});
  Check(second.previous && second.previous->vertex[0] == 20,
        "instance B used another pose");
  vertices[0] = 11;
  auto first = normal.Record(a, vertices, shared, {});
  Check(first.previous && first.previous->vertex[0] == 10,
        "draw reorder changed instance history");
  auto changed = a;
  changed.geometry_generation = 9;
  Check(!normal.Record(changed, vertices, shared, {}).previous,
        "replaced geometry reused old vertex motion");
  vertices[1] = 1;
  Check(normal.Record(a, vertices, shared, {}).ambiguous,
        "same-frame identity conflict was hidden");
  Check(normal.Begin(1, 1, 3, false), "geometry third frame");
  Check(!normal.Record(a, vertices, shared, {}).previous,
        "ambiguous prior instance used motion");
  Check(normal.Begin(1, 1, 5, false), "geometry gap");
  Check(!normal.Record(b, vertices, shared, {}).previous,
        "hidden instance reappeared with stale motion");
  Check(normal.Begin(2, 1, 1, true), "new scene epoch");
  Check(!normal.Begin(1, 1, 6, false), "stale epoch replaced geometry history");
  normal.Reset();
  Check(first.previous && first.previous->vertex[0] == 10,
        "snapshot lease was invalidated by reset");
  first = {};
  second = {};
  Check(normal.owned_bytes() == 0,
        "geometry snapshot budget outlived its last lease");
  std::printf("temporal_geometry_identity_budget=passed\n");
}

void HistoryTests() {
  History h;
  auto f = MakeFrame(1);
  Check(h.Inspect(f).reset && h.Inspect(f).accepted, "first frame history");
  h.Commit(f);
  Check(!h.Inspect(f).accepted, "duplicate accepted");
  auto next = MakeFrame(2);
  Check(h.Inspect(next).accepted && !h.Inspect(next).reset,
        "continuous frame reset");
  for (auto coverage : {InputCoverage::kUnknown, InputCoverage::kCameraOnly}) {
    auto bad = next;
    bad.coverage = coverage;
    Check(!h.Inspect(bad).accepted,
          "camera-only motion accepted as full scene");
    Check(!h.Inspect(bad, true).accepted,
          "reactive AA admission accepted missing motion");
  }
  auto reactive_scene = next;
  reactive_scene.coverage = InputCoverage::kReactiveSceneMotion;
  Check(h.Inspect(reactive_scene, true).accepted &&
            !h.Inspect(reactive_scene, true).reset,
        "AA rejected scene motion with explicit reactive coverage");
  Check(!h.Inspect(reactive_scene).accepted,
        "frame interpolation accepted incomplete reactive scene motion");
  for (int field = 0; field < 9; ++field) {
    auto bad = next;
    switch (field) {
    case 0:
      bad.epoch = 0;
      break;
    case 1:
      bad.view = 0;
      break;
    case 2:
      bad.time_ns = f.time_ns;
      break;
    case 3:
      bad.jitter[0] = NAN;
      break;
    case 4:
      bad.jitter[1] = 1;
      break;
    case 5:
      bad.near_plane = 0;
      break;
    case 6:
      bad.color_domain = ColorDomain::kUnknown;
      break;
    case 7:
      bad.scene_without_ui = false;
      break;
    case 8:
      bad.jitter_applied = false;
      break;
    }
    Check(!h.Inspect(bad).accepted, "invalid temporal metadata accepted");
  }
  for (int field = 0; field < 6; ++field) {
    auto cut = next;
    switch (field) {
    case 0:
      cut.camera_cut = true;
      break;
    case 1:
      cut.epoch = 2;
      break;
    case 2:
      cut.view = 2;
      break;
    case 3:
      cut.sequence = 3;
      break;
    case 4:
      cut.time_ns += 1000000000;
      break;
    case 5:
      cut.field_of_view = 70;
      break;
    }
    Check(h.Inspect(cut).accepted && h.Inspect(cut).reset,
          "discontinuity reused history");
  }
  auto new_epoch = MakeFrame(1);
  new_epoch.epoch = 2;
  h.Commit(new_epoch);
  Check(!h.Inspect(next).accepted, "stale epoch accepted");
  for (uint64_t i = 0; i < 1024; ++i) {
    auto j = Jitter(i);
    for (float x : j)
      Check(x >= -0.5f && x <= 0.5f, "jitter range");
    Check(Jitter(i) == Jitter(i + 32), "jitter sequence repeat");
  }
  PairDelivery pair;
  Check(pair.Begin(1, 1, false), "prime real");
  Check(pair.kind() == PairDelivery::Kind::kReal && pair.Consume(),
        "first real acknowledgement");
  Check(pair.Begin(1, 2, true), "pair admission");
  Check(!pair.Begin(1, 3, true), "pending pair overwritten");
  Check(pair.kind() == PairDelivery::Kind::kGenerated && !pair.Consume(),
        "generated image acknowledged game");
  Check(pair.kind() == PairDelivery::Kind::kReal && pair.Consume(),
        "real was not delivered after midpoint");
  Check(!pair.Begin(1, 2, true), "repeated pair replayed");
  Check(pair.Begin(1, 3, true) && pair.Begin(2, 1, false),
        "new epoch did not discard old pair");
  Check(!pair.Begin(1, 4, true), "stale epoch revived pair");
  std::printf("temporal_history_and_pair_policy=passed\n");
}
void ExposureTests(Fixture &f) {
  std::string error;
  AntiAliasing aa;
  Check(aa.Configure(f.device, f.library, {64, 64, 64, 64, Method::kMetalFx, false},
                     16u * 1024u * 1024u, error), error);
  auto inputs = f.Textures(64, 64);
  const Pixel expected{0.25f, 0.5f, 0.75f, 1};
  f.Uniform(inputs, expected);
  uint64_t sequence = 1;
  // Exposure is an analysis hint, not a brightness multiplier on the output.
  // Non-unit values exercise the real half-float upload and scaler on the GPU.
  for (float exposure : {0.125f, 0.5f, 2.0f, 8.0f}) {
    auto frame = MakeFrame(sequence++);
    frame.exposure = exposure;
    frame.camera_cut = true;
    auto commands = [f.queue commandBuffer];
    Output output;
    Check(aa.Encode(commands, inputs, frame, output, error), error);
    const auto pixels = f.Read(commands, output.color);
    for (const auto &pixel : pixels)
      for (size_t channel = 0; channel < expected.size(); ++channel)
        Check(std::abs(pixel[channel] - expected[channel]) < 0.012f,
              "MetalFX exposure hint changed uniform output brightness: exposure=" +
                  std::to_string(exposure) + " channel=" + std::to_string(channel) +
                  " value=" + std::to_string(pixel[channel]));
  }
  // Both inputs are valid positive floats but cannot be uploaded as a positive
  // finite R16Float texel. Reject them without consuming temporal history.
  for (float exposure : {std::numeric_limits<float>::denorm_min(),
                         std::numeric_limits<float>::max()}) {
    auto frame = MakeFrame(sequence);
    frame.exposure = exposure;
    auto commands = [f.queue commandBuffer];
    Output rejected;
    Check(!aa.Encode(commands, inputs, frame, rejected, error) && !rejected.color &&
              error.find("half-float") != std::string::npos,
          "Unrepresentable MetalFX exposure was accepted");
  }
  auto commands = [f.queue commandBuffer];
  Output recovered;
  Check(aa.Encode(commands, inputs, MakeFrame(sequence), recovered, error),
        "Rejected exposure consumed temporal history: " + error);
  f.Read(commands, recovered.color);
  recovered = {};
  // A GPU-produced exposure texture must take precedence over the scalar hint.
  // An unrepresentable scalar proves that this path does not silently upload it.
  for (float hint : {0.25f, 4.0f}) {
    inputs.exposure = f.Image(MTLPixelFormatR16Float, 1, 1);
    const _Float16 value = _Float16(hint);
    [inputs.exposure replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
                         mipmapLevel:0 withBytes:&value bytesPerRow:sizeof(value)];
    auto frame = MakeFrame(++sequence);
    frame.camera_cut = true;
    frame.exposure = std::numeric_limits<float>::max();
    commands = [f.queue commandBuffer];
    Output output;
    Check(aa.Encode(commands, inputs, frame, output, error),
          "GPU exposure override failed: " + error);
    for (const auto &pixel : f.Read(commands, output.color))
      for (size_t channel = 0; channel < expected.size(); ++channel)
        Check(std::abs(pixel[channel] - expected[channel]) < 0.012f,
              "GPU exposure hint changed uniform scene brightness");
  }
  for (auto bad : {f.Image(MTLPixelFormatR32Float, 1, 1),
                   f.Image(MTLPixelFormatR16Float, 2, 1)}) {
    inputs.exposure = bad;
    commands = [f.queue commandBuffer];
    Output output;
    Check(!aa.Encode(commands, inputs, MakeFrame(sequence + 1), output, error),
          "MetalFX accepted invalid GPU exposure format or extent");
  }
  std::printf("metalfx_exposure_half_float=passed hints=4 range_rejections=2\n");
}
void AntiAliasingTests(Fixture &f) {
  for (auto method : {Method::kTaa, Method::kMetalFx})
    for (bool reversed : {false, true}) {
      std::string error;
      AntiAliasing aa;
      Configuration c{64, 64, 64, 64, method, reversed};
      Check(aa.Configure(f.device, f.library, c, 16u * 1024u * 1024u, error),
            error);
      const auto reserved = aa.owned_bytes();
      Check(reserved > 0, "missing AA history");
      auto in = f.Textures(64, 64);
      f.Uniform(in, {0.25f, 0.5f, 0.75f, 1});
      for (uint64_t i = 1; i <= 8; ++i) {
        auto commands = [f.queue commandBuffer];
        Output output;
        auto frame = MakeFrame(i);
        frame.jitter = Jitter(i);
        Check(aa.Encode(commands, in, frame, output, error), error);
        Check(output.history_reset == (i == 1), "AA reset state");
        const auto image = f.Read(commands, output.color);
        for (const auto &p : image)
          for (size_t k = 0; k < 4; ++k)
            Check(std::abs(p[k] - Pixel{0.25f, 0.5f, 0.75f, 1}[k]) < 0.012f,
                  "temporal AA changed uniform input");
        Check(aa.owned_bytes() == reserved, "history grew per frame");
      }
      auto commands = [f.queue commandBuffer];
      Output rejected;
      Check(!aa.Encode(commands, in, MakeFrame(8), rejected, error) &&
                !rejected.color,
            "AA duplicated frame");
      auto incomplete = MakeFrame(9);
      incomplete.coverage = InputCoverage::kCameraOnly;
      Check(!aa.Encode(commands, in, incomplete, rejected, error),
            "AA guessed object motion");
      const auto valid = in.depth;
      in.depth = f.Image(MTLPixelFormatR32Float, 32, 32);
      Check(!aa.Encode(commands, in, MakeFrame(9), rejected, error),
            "AA accepted misregistered depth");
      in.depth = valid;
      Check(!aa.Configure(f.device, f.library,
                          {64, 64, 64, 64, method, reversed}, 1, error),
            "AA exceeded budget");
      Check(aa.owned_bytes() == reserved,
            "failed AA configure destroyed history");
      f.Uniform(in, {0.75f, 0.25f, 0.5f, 1});
      auto cut = MakeFrame(9);
      cut.camera_cut = true;
      Output output;
      Check(aa.Encode(commands, in, cut, output, error) && output.history_reset,
            error);
      const auto pixels = f.Read(commands, output.color);
      for (const auto &p : pixels)
        for (size_t k = 0; k < 4; ++k)
          Check(std::abs(p[k] - Pixel{0.75f, 0.25f, 0.5f, 1}[k]) < 0.012f,
                "camera cut retained old color");
      std::printf(
          "temporal_aa_uniform_reset=passed method=%u reverse=%u owned=%zu\n",
          unsigned(method), unsigned(reversed), reserved);
    }
  // Native reprojection direction and depth/reactivity rejection, with known
  // motion independent of the shader: a striped field translates right.
  std::string error;
  AntiAliasing aa;
  Check(aa.Configure(f.device, f.library, {64, 64, 64, 64, Method::kTaa, false},
                     16u * 1024u * 1024u, error),
        error);
  auto in = f.Textures(64, 64);
  f.Uniform(in, {0, 0, 0, 1});
  std::vector<Pixel> prior(64 * 64), current(64 * 64);
  for (size_t y = 0; y < 64; ++y)
    for (size_t x = 0; x < 64; ++x) {
      float v = float((x / 4) % 2);
      prior[y * 64 + x] = {v, 0.2f + v * 0.6f, 1 - v, 0.6f};
      size_t old = x >= 3 ? x - 3 : 0;
      float t = float((old / 4) % 2);
      current[y * 64 + x] = {t, 0.2f + t * 0.6f, 1 - t, 0.6f};
    }
  f.WriteColor(in.color, prior);
  Output output;
  auto commands = [f.queue commandBuffer];
  Check(aa.Encode(commands, in, MakeFrame(1), output, error), error);
  f.Read(commands, output.color);
  f.WriteColor(in.color, current);
  f.WriteMotion(in.motion, std::vector<std::array<float, 2>>(64 * 64, {-3, 0}));
  commands = [f.queue commandBuffer];
  Check(aa.Encode(commands, in, MakeFrame(2), output, error), error);
  auto pixels = f.Read(commands, output.color);
  for (size_t y = 2; y < 62; ++y)
    for (size_t x = 5; x < 60; ++x)
      for (size_t k = 0; k < 4; ++k)
        Check(std::abs(pixels[y * 64 + x][k] - current[y * 64 + x][k]) < 0.005f,
              "TAA reprojection used the wrong motion direction");
  f.Reactive(in.reactive, 255);
  f.Uniform(in, {0.1f, 0.2f, 0.3f, 0.5f});
  f.Reactive(in.reactive, 255);
  commands = [f.queue commandBuffer];
  Check(aa.Encode(commands, in, MakeFrame(3), output, error), error);
  pixels = f.Read(commands, output.color);
  for (const auto &p : pixels)
    for (size_t k = 0; k < 4; ++k)
      Check(std::abs(p[k] - Pixel{0.1f, 0.2f, 0.3f, 0.5f}[k]) < 0.003f,
            "reactive pixel reused history");
  std::printf("native_taa_motion_alpha_reactivity=passed\n");
  // No CPU wait is hidden in the production effect; exhausted output slots fail
  // before writing a texture that unsubmitted GPU work still references.
  aa.Reset();
  commands = [f.queue commandBuffer];
  for (uint64_t i = 10; i < 13; ++i)
    Check(aa.Encode(commands, in, MakeFrame(i), output, error), error);
  Check(!aa.Encode(commands, in, MakeFrame(13), output, error) && !output.color,
        "in-flight AA history overwritten");
  [commands commit];
  [commands waitUntilCompleted];
  Check(commands.status == MTLCommandBufferStatusCompleted,
        "in-flight AA test failed");
}

void NativeDepthAndFootprintTests(Fixture &f) {
  constexpr uint32_t size = 16;
  constexpr size_t pixels_count = size * size;
  const auto device_depth = [](float view, bool reverse) {
    // Independent perspective projection, evaluated in double precision.
    constexpr double near = 0.1, far = 1000;
    const double projected = (far - near * far / double(view)) / (far - near);
    return float(reverse ? 1 - projected : projected);
  };
  struct DepthCase { const char *name; float current, previous; bool reject; };
  const std::array depth_cases{
      DepthCase{"camera-approach", 9.5f, 10, false},
      DepthCase{"camera-retreat", 10.5f, 10, false},
      DepthCase{"object-approach", 8, 10, false},
      DepthCase{"newly-revealed-surface", 9.5f, 12, true},
      DepthCase{"unsupported-depth-viewport", 9.5f, -1, true},
      DepthCase{"nonfinite-prior-surface", 9.5f, NAN, true}};
  std::vector<Pixel> checker(pixels_count);
  for (size_t y = 0; y < size; ++y)
    for (size_t x = 0; x < size; ++x) {
      const float v = x % 2 ? 0.75f : 0.25f;
      checker[y * size + x] = {v, v, v, 1};
    }
  for (bool reverse : {false, true})
    for (const auto &test : depth_cases) {
      std::string error;
      AntiAliasing aa;
      Check(aa.Configure(f.device, f.library,
                         {size, size, size, size, Method::kTaa, reverse},
                         16u * 1024u * 1024u, error), error);
      auto in = f.Textures(size, size);
      f.Uniform(in, {0.5f, 0.5f, 0.5f, 1}, device_depth(10, reverse));
      auto first = MakeFrame(1);
      first.aspect = 1.5f;  // Projection aspect need not equal target aspect.
      auto commands = [f.queue commandBuffer];
      Output out;
      Check(aa.Encode(commands, in, first, out, error), error);
      f.Read(commands, out.color);
      out = {};
      f.WriteColor(in.color, checker);
      f.WriteDepth(in.depth, std::vector<float>(pixels_count, device_depth(test.current, reverse)));
      const float expected_previous = test.previous > 0 ? device_depth(test.previous, reverse) : test.previous;
      f.WriteDepth(in.previous_depth, std::vector<float>(pixels_count, expected_previous));
      auto second = MakeFrame(2);
      second.aspect = first.aspect;
      second.coverage = InputCoverage::kReactiveSceneMotion;
      commands = [f.queue commandBuffer];
      Check(aa.Encode(commands, in, second, out, error) && !out.history_reset, error);
      auto pixels = f.Read(commands, out.color);
      for (size_t y = 2; y < size - 2; ++y)
        for (size_t x = 2; x < size - 2; ++x) {
          const float wanted = test.reject ? checker[y * size + x][0]
                                          : (x % 2 ? 0.525f : 0.475f);
          Check(std::abs(pixels[y * size + x][0] - wanted) < 0.003f,
                std::string("native depth ") + test.name + " reverse=" +
                    std::to_string(reverse) + " xy=" + std::to_string(x) + "," +
                    std::to_string(y) + " actual=" +
                    std::to_string(pixels[y * size + x][0]) + " expected=" + std::to_string(wanted));
        }
    }
  // All nonzero Halton phases, both edge orientations, and independently
  // invalid reactivity, prior depth, current depth, and foreground motion.
  for (unsigned axis : {0u, 1u})
    for (uint64_t phase = 1; phase <= 32; ++phase)
      for (unsigned invalidity = 0; invalidity < 4; ++invalidity) {
        auto jitter = Jitter(phase);
        if (std::abs(jitter[axis]) < 1.0e-6f) continue;
        std::string error;
        AntiAliasing aa;
        Check(aa.Configure(f.device, f.library,
                           {size, size, size, size, Method::kTaa, false},
                           16u * 1024u * 1024u, error), error);
        auto in = f.Textures(size, size);
        f.Uniform(in, {0, 0, 0, 1});
        auto commands = [f.queue commandBuffer];
        Output out;
        Check(aa.Encode(commands, in, MakeFrame(1), out, error), error);
        f.Read(commands, out.color);
        out = {};
        std::vector<Pixel> color(pixels_count, Pixel{0, 0, 0, 1});
        std::vector<float> depth(pixels_count, 0.5f), previous(pixels_count, 0.5f);
        std::vector<std::array<float, 2>> motion(pixels_count, {0, 0});
        std::vector<uint8_t> reactive(pixels_count, 0);
        constexpr size_t center = size / 2;
        const size_t feature = jitter[axis] > 0 ? center + 1 : center - 1;
        for (size_t y = 0; y < size; ++y)
          for (size_t x = 0; x < size; ++x) {
            if ((axis ? y : x) != feature) continue;
            const auto i = y * size + x;
            color[i] = {1, 1, 1, 1};
            if (invalidity == 0) reactive[i] = 255;
            if (invalidity == 1) previous[i] = -1;
            if (invalidity == 2) depth[i] = previous[i] = 0.2f;
            if (invalidity == 3) motion[i][axis] = 2;
          }
        f.WriteColor(in.color, color);
        f.WriteDepth(in.depth, depth);
        f.WriteDepth(in.previous_depth, previous);
        f.WriteMotion(in.motion, motion);
        [in.reactive replaceRegion:MTLRegionMake2D(0, 0, size, size)
                       mipmapLevel:0 withBytes:reactive.data() bytesPerRow:size];
        auto frame = MakeFrame(2);
        frame.jitter = jitter;
        commands = [f.queue commandBuffer];
        Check(aa.Encode(commands, in, frame, out, error), error);
        const auto pixels = f.Read(commands, out.color);
        const float wanted = std::abs(jitter[axis]);
        Check(std::abs(pixels[center * size + center][0] - wanted) < 0.003f,
              "native thin-edge footprint axis=" + std::to_string(axis) +
                  " phase=" + std::to_string(phase) + " invalidity=" +
                  std::to_string(invalidity) + " actual=" +
                  std::to_string(pixels[center * size + center][0]) +
                  " expected=" + std::to_string(wanted));
      }
  {
    std::string error;
    AntiAliasing aa;
    Check(aa.Configure(f.device, f.library,
                       {size, size, size, size, Method::kTaa, false},
                       16u * 1024u * 1024u, error), error);
    auto in = f.Textures(size, size);
    f.Uniform(in, {0.5f, 0.5f, 0.5f, 1});
    std::vector<float> prior_depth(pixels_count, 0.5f);
    for (size_t y = 0; y < size; ++y) prior_depth[y * size + size / 2 + 1] = 0.2f;
    f.WriteDepth(in.depth, prior_depth);
    auto commands = [f.queue commandBuffer];
    Output out;
    Check(aa.Encode(commands, in, MakeFrame(1), out, error), error);
    f.Read(commands, out.color);
    out = {};
    f.Uniform(in, {0, 0, 0, 1});
    f.WriteColor(in.color, checker);
    f.WriteMotion(in.motion, std::vector<std::array<float, 2>>(pixels_count, {0.25f, 0}));
    commands = [f.queue commandBuffer];
    Check(aa.Encode(commands, in, MakeFrame(2), out, error), error);
    const auto pixels = f.Read(commands, out.color);
    Check(std::abs(pixels[(size / 2) * size + size / 2][0] - 0.25f) < 0.003f,
          "bilinear history reused a neighboring occluder's color");
  }
  std::printf("native_taa_depth_space_and_reconstruction_footprints=passed\n");
}

void ConvergenceAndLifetime(Fixture &f) {
  {
    auto commands = [f.queue commandBuffer];
    auto lease = std::make_shared<int>(42);
    std::weak_ptr<void> observed = lease;
    KeepUntilCompleted(commands, lease);
    lease.reset();
    Check(!observed.expired(), "output lease released before GPU submission");
    [commands commit];
    [commands waitUntilCompleted];
    Check(observed.expired(),
          "completed command kept a temporal output leased");
    Check(commands != nil,
          "completion-lifetime test lost its retained command buffer");
  }

  constexpr uint32_t size = 64;
  std::vector<Pixel> source(size * size);
  std::vector<double> reference(size * size);
  // Independent box integration of a half-plane; none of the temporal kernels
  // compute this reference or supply the jittered frame's edge coverage.
  for (uint32_t y = 0; y < size; ++y)
    for (uint32_t x = 0; x < size; ++x) {
      size_t covered = 0;
      for (uint32_t sy = 0; sy < 32; ++sy)
        for (uint32_t sx = 0; sx < 32; ++sx)
          covered += double(x) + (double(sx) + 0.5) / 32 >
                     0.37 * (double(y) + (double(sy) + 0.5) / 32) + 17.2;
      reference[y * size + x] = double(covered) / (32 * 32);
    }
  for (auto method : {Method::kTaa, Method::kMetalFx}) {
    std::string error;
    AntiAliasing aa;
    Check(aa.Configure(f.device, f.library,
                       {size, size, size, size, method, false},
                       16u * 1024u * 1024u, error),
          error);
    auto in = f.Textures(size, size);
    f.Uniform(in, {0, 0, 0, 1});
    double raw_error = 0, temporal_error = 0;
    Output output;
    for (uint64_t sequence = 1; sequence <= 32; ++sequence) {
      auto frame = MakeFrame(sequence);
      frame.jitter = Jitter(sequence);
      for (uint32_t y = 0; y < size; ++y)
        for (uint32_t x = 0; x < size; ++x) {
          const bool covered =
              double(x) + 0.5 - frame.jitter[0] >
              0.37 * (double(y) + 0.5 - frame.jitter[1]) + 17.2;
          source[y * size + x] = {covered ? 1.0f : 0.0f, covered ? 1.0f : 0.0f,
                                  covered ? 1.0f : 0.0f, 1};
        }
      f.WriteColor(in.color, source);
      auto commands = [f.queue commandBuffer];
      Check(aa.Encode(commands, in, frame, output, error), error);
      auto result = f.Read(commands, output.color);
      if (sequence > 16)
        for (size_t i = 0; i < result.size(); ++i) {
          raw_error += std::pow(double(source[i][0]) - reference[i], 2);
          temporal_error += std::pow(double(result[i][0]) - reference[i], 2);
        }
    }
    std::printf("temporal_aa_convergence method=%u raw_mse_sum=%g "
                "resolved_mse_sum=%g\n",
                unsigned(method), raw_error, temporal_error);
    Check(temporal_error < raw_error * 0.8,
          "Temporal AA failed to reduce aliasing of a jittered geometric edge");
    const size_t reserved = aa.owned_bytes();
    output = {};
    std::array<Output, 3> held;
    for (size_t i = 0; i < held.size(); ++i) {
      auto commands = [f.queue commandBuffer];
      Check(aa.Encode(commands, in, MakeFrame(33 + i), held[i], error), error);
      f.Read(commands, held[i].color);
    }
    auto commands = [f.queue commandBuffer];
    Check(!aa.Encode(commands, in, MakeFrame(36), output, error),
          "leased output was overwritten after GPU completion");
    held[0] = {};
    Check(aa.Encode(commands, in, MakeFrame(36), output, error), error);
    f.Read(commands, output.color);
    Check(aa.owned_bytes() == reserved, "output lease enlarged the ring");
    auto wrong_queue = [f.device newCommandQueue];
    auto other = [wrong_queue commandBuffer];
    Check(!aa.Encode(other, in, MakeFrame(37), output, error),
          "temporal history crossed queues without ordering");
    auto old = held[1];
    Check(aa.Configure(f.device, f.library, {32, 32, 32, 32, method, false},
                       16u * 1024u * 1024u, error),
          error);
    auto small = f.Textures(32, 32);
    f.Uniform(small, {0.2f, 0.4f, 0.6f, 1});
    commands = [f.queue commandBuffer];
    Check(aa.Encode(commands, small, MakeFrame(40), output, error) &&
              output.history_reset,
          error);
    f.Read(commands, output.color);
    auto inspect = [f.queue commandBuffer];
    Check(old.color.width == 64 && old.lease,
          "resize invalidated a leased previous output");
    f.Read(inspect, old.color);
  }
  std::printf("temporal_convergence_resize_leases=passed\n");
}

Frame DisplayFrame(uint64_t sequence) {
  auto f = MakeFrame(sequence);
  f.color_domain = ColorDomain::kDisplayLinear;
  return f;
}
void InterpolationTests(Fixture &f) {
  std::string error;
  FrameInterpolation interpolation;
  Configuration c{128, 128, 128, 128, Method::kMetalFx, false};
  Check(interpolation.Configure(f.device, f.library, c, 32u * 1024u * 1024u,
                                error),
        error);
  const auto owned = interpolation.owned_bytes();
  auto in = f.Textures(128, 128);
  f.Uniform(in, {0.25f, 0.5f, 0.75f, 1});
  for (uint64_t i = 1; i <= 4; ++i) {
    auto commands = [f.queue commandBuffer];
    InterpolationOutput out;
    Check(interpolation.Encode(commands, in, {}, DisplayFrame(i), out, error),
          error);
    Check(out.history_reset == (i == 1), "frame generation reset mismatch");
    Check(bool(out.generated) == (i > 1),
          "generation fabricated a missing pair");
    const auto pixels = f.Read(commands, i > 1 ? out.generated : out.real);
    for (const auto &p : pixels)
      for (size_t k = 0; k < 3; ++k)
        Check(std::abs(p[k] - Pixel{0.25f, 0.5f, 0.75f, 1}[k]) < 0.012f,
              "frame interpolation changed constant color");
    Check(interpolation.owned_bytes() == owned, "interpolation snapshots grew");
  }
  auto commands = [f.queue commandBuffer];
  InterpolationOutput out;
  auto incomplete = DisplayFrame(5);
  incomplete.coverage = InputCoverage::kCameraOnly;
  Check(!interpolation.Encode(commands, in, {}, incomplete, out, error),
        "frame interpolation accepted fake object motion");
  auto cut = DisplayFrame(5);
  cut.camera_cut = true;
  Check(interpolation.Encode(commands, in, {}, cut, out, error) &&
            !out.generated && out.history_reset,
        error);
  f.Read(commands, out.real);
  std::printf("metalfx_interpolation_uniform_reset=passed owned=%zu\n", owned);
  // A known moving opaque object and independent, stationary composited UI.
  interpolation.Reset();
  std::vector<Pixel> scene(128 * 128), ui(128 * 128);
  std::vector<float> depth(128 * 128);
  std::vector<std::array<float, 2>> velocity(128 * 128);
  std::vector<Pixel> generated;
  for (uint64_t step = 0; step < 4; ++step) {
    const uint32_t left = 32 + uint32_t(step) * 4;
    for (uint32_t y = 0; y < 128; ++y)
      for (uint32_t x = 0; x < 128; ++x) {
        const size_t i = y * 128 + x;
        bool object = x >= left && x < left + 24 && y >= 42 && y < 85;
        scene[i] = object ? Pixel{1, 1, 1, 1} : Pixel{0, 0, 0, 1};
        depth[i] = object ? 0.2f : 0.9f;
        velocity[i] =
            object ? std::array<float, 2>{-4, 0} : std::array<float, 2>{0, 0};
        ui[i] =
            (x < 12 && y < 12) ? Pixel{0.1f, 0.8f, 0.2f, 1} : Pixel{0, 0, 0, 0};
      }
    f.WriteColor(in.color, scene);
    f.WriteDepth(in.depth, depth);
    f.WriteMotion(in.motion, velocity);
    auto overlay = f.Image(MTLPixelFormatRGBA16Float, 128, 128);
    f.WriteColor(overlay, ui);
    commands = [f.queue commandBuffer];
    Check(interpolation.Encode(commands, in,
                               {UiMode::kPremultipliedOverlay, overlay},
                               DisplayFrame(step + 10), out, error),
          error);
    generated = f.Read(commands, out.generated ? out.generated : out.real);
    for (uint32_t y = 2; y < 10; ++y)
      for (uint32_t x = 2; x < 10; ++x)
        for (size_t k = 0; k < 3; ++k)
          Check(std::abs(generated[y * 128 + x][k] - ui[y * 128 + x][k]) <
                    0.015f,
                "frame generation warped stationary UI");
    if (step >= 2) {
      double sum = 0, weighted = 0;
      for (uint32_t y = 24; y < 100; ++y)
        for (uint32_t x = 0; x < 128; ++x) {
          double value =
              std::clamp(double(generated[y * 128 + x][0]), 0.0, 1.0);
          sum += value;
          weighted += value * x;
        }
      const double wanted = double(left) - 2 + double(24 - 1) / 2;
      Check(sum > 40, "interpolated object disappeared");
      const double center = weighted / sum;
      std::printf("metalfx_moving_object step=%llu centroid=%g midpoint=%g\n",
                  (unsigned long long)step, center, wanted);
      Check(std::abs(center - wanted) < 2.0,
            "generated object did not move to the midpoint");
    }
  }
  std::printf("metalfx_interpolation_object_and_ui=passed\n");
}
} // namespace
int main(int argc, char **argv) {
  @autoreleasepool {
    try {
      Check(argc == 2 || argc == 3,
            "usage: metal-temporal-test temporal.metallib [--aa-only]");
      HistoryTests();
      GeometryIdentityTests();
      Fixture f(argv[1]);
      LargeHistoryAllocation(f);
      TestMotionCoverage(f.device, f.queue, f.library);
      const auto c = QueryCapabilities(f.device);
      Check(c.temporal && c.interpolation,
            "device lacks requested MetalFX capabilities");
      ExposureTests(f);
      AntiAliasingTests(f);
      NativeDepthAndFootprintTests(f);
      ConvergenceAndLifetime(f);
      if (argc != 3)
        InterpolationTests(f);
      else
        Check(std::string(argv[2]) == "--aa-only",
              "unknown temporal test option");
      std::printf("metal_temporal_gpu=passed cases=%zu checks=%zu device=%s\n",
                  cases, checks, f.device.name.UTF8String);
      return 0;
    } catch (const std::exception &e) {
      std::fprintf(stderr, "Temporal test: %s\n", e.what());
      return 1;
    }
  }
}
