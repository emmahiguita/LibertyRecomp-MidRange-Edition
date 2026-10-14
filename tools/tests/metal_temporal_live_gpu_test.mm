// Shared independent pixel/readback fixtures, followed by the production live
// integration class. The synthetic scene isolates depth lifetime,
// reconstruction, output resolution, isolated UI, interpolation and leases from
// game assets.
#define main temporal_component_main
#include "metal_temporal_gpu_test.mm"
#undef main
#include "graphics/gta4_metal/temporal/live.h"
#include "graphics/gta4_metal/temporal/projection.h"

namespace {
using namespace rex::graphics::gta4_metal;
void Fill(id<MTLCommandBuffer> cb, id<MTLTexture> image, MTLClearColor color) {
  auto p = [MTLRenderPassDescriptor renderPassDescriptor];
  p.colorAttachments[0].texture = image;
  p.colorAttachments[0].loadAction = MTLLoadActionClear;
  p.colorAttachments[0].storeAction = MTLStoreActionStore;
  p.colorAttachments[0].clearColor = color;
  auto e = [cb renderCommandEncoderWithDescriptor:p];
  Check(e != nil, "live color clear");
  [e endEncoding];
}
void DepthFill(id<MTLCommandBuffer> cb, id<MTLTexture> image, double value) {
  auto p = [MTLRenderPassDescriptor renderPassDescriptor];
  p.depthAttachment.texture = image;
  p.depthAttachment.loadAction = MTLLoadActionClear;
  p.depthAttachment.storeAction = MTLStoreActionStore;
  p.depthAttachment.clearDepth = value;
  p.stencilAttachment.texture = image;
  p.stencilAttachment.loadAction = MTLLoadActionClear;
  p.stencilAttachment.storeAction = MTLStoreActionStore;
  auto e = [cb renderCommandEncoderWithDescriptor:p];
  Check(e != nil, "live depth clear");
  [e endEncoding];
}
id<MTLTexture> LiveImage(Fixture &f, MTLPixelFormat format, uint32_t size) {
  auto d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                              width:size
                                                             height:size
                                                          mipmapped:NO];
  d.storageMode = MTLStorageModePrivate;
  d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
  if (format != MTLPixelFormatDepth32Float_Stencil8)
    d.usage |= MTLTextureUsageShaderWrite;
  auto image = [f.device newTextureWithDescriptor:d];
  Check(image != nil, "live fixture allocation");
  return image;
}
void ExposureHint(Fixture& f, Live& live, id<MTLCommandBuffer> commands,
                  float adaptation = 1, float exposure = 1, float tone = 1) {
  auto input = f.Image(MTLPixelFormatR32Float, 1, 1);
  f.WriteDepth(input, {adaptation});
  std::string error;
  Check(live.SetExposure(commands, input, exposure, tone, error), error);
}
id<MTLCommandBuffer> ValidateMotion(Fixture& f, Live& live, id<MTLCommandBuffer> commands) {
  if (!live.NeedsMotionCoverage()) return commands;
  std::string error;
  Check(live.ValidateMotionCoverage(commands, error), error);
  [commands commit];
  [commands waitUntilCompleted];
  Check(commands.status == MTLCommandBufferStatusCompleted,
        commands.error ? commands.error.localizedDescription.UTF8String : "motion validation submission");
  return [f.queue commandBuffer];
}
void CameraTests() {
  for (bool reverse : {false, true})
    for (float near : {0.1f, 0.5f, 1.0f})
      for (float far : {100.0f, 1000.0f, 4000.0f}) {
        std::array<float, 16> p{};
        p[0] = 1;
        p[5] = 1.5f;
        p[11] = 1;
        p[10] = reverse ? near / (near - far) : far / (far - near);
        p[14] =
            reverse ? near * far / (far - near) : -near * far / (far - near);
        auto camera = DescribeProjection(p);
        Check(camera.has_value(), "perspective camera was rejected");
        Check(camera->reversed == reverse, "projection depth convention");
        Check(std::abs(camera->near_plane - near) < 0.001f,
              "projection near plane");
        Check(std::abs(camera->far_plane - far) < far * 0.003f,
              "projection far plane");
        Check(camera->aspect == 1.5f, "projection aspect");
      }
  std::array<float, 16> ortho{};
  ortho[0] = ortho[5] = ortho[10] = ortho[15] = 1;
  Check(!DescribeProjection(ortho),
        "orthographic map accepted as main perspective camera");
  std::printf("live_camera_projection=passed\n");
}
void LiveChain(Fixture &f, Method method, bool upscaled) {
  std::string error;
  auto context = std::make_shared<rex::ui::metal::MetalContext>();
  context->device = f.device;
  context->queue = f.queue;
  Live live;
  Check(live.Initialize(context, error), error);
  const uint32_t input = 64, output = upscaled ? 128 : 64;
  auto scene = f.Textures(input, input);
  f.Uniform(scene, {0.25f, 0.5f, 0.75f, 1});
  auto depth = std::make_shared<SurfaceResource>();
  depth->image = LiveImage(f, MTLPixelFormatDepth32Float_Stencil8, input);
  depth->depth = true;
  auto display = LiveImage(f, MTLPixelFormatRGBA16Float, output);
  for (uint64_t sequence = 1; sequence <= 6; ++sequence) {
    @autoreleasepool {
      auto commands = [f.queue commandBuffer];
      rex::graphics::gta4_native::TemporalCommand c;
      c.event = rex::graphics::gta4_native::TemporalEvent::kBeginScene;
      c.device = 1;
      c.view = 2;
      c.epoch = 1;
      c.sequence = sequence;
      c.time_ns = 1000000000 + sequence * 16666667;
      c.width = c.height = input;
      c.output_width = c.output_height = output;
      c.flags = rex::graphics::gta4_native::kTemporalJitterApplied;
      c.jitter = Jitter(sequence);
      c.aspect = 1;
      if (sequence == 4)
        c.flags |= rex::graphics::gta4_native::kTemporalCameraCut;
      Check(live.Begin(commands, c, method, output, output, true, error),
            error);
      ExposureHint(f, live, commands);
      DepthFill(commands, depth->image, 0.4);
      depth->content_serial = sequence * 2;
      live.depth_source = depth;
      live.world_draws = 1;
      live.history_draws = 1;
      live.jittered_draws = 1;
      Fill(commands, live.inputs.reactive, MTLClearColorMake(0, 0, 0, 0));
      Fill(commands, live.inputs.previous_depth, MTLClearColorMake(0.4, 0, 0, 0));
      Check(live.CaptureDepth(commands, error), error);
      // Exercise the actual title resolve-clear dependency: AA must not
      // subsequently recapture an attachment that the game already cleared for
      // another pass.
      DepthFill(commands, depth->image, 0.9);
      ++depth->content_serial;
      auto reconstructed = live.Resolve(commands, scene.color, error);
      Check(reconstructed != nil, error);
      Check(reconstructed.width == output && reconstructed.height == output,
            "MetalFX live output dimensions");
      auto high = live.HighComposite(MTLPixelFormatRGBA16Float, error);
      Check(bool(high), error);
      Check(live.CopyColor(commands, reconstructed, high->image, error), error);
      Check(live.AfterComposite(commands, high->image, error), error);
      Check(live.StartUi(commands, error), error);
      Fill(commands, live.ui_add, MTLClearColorMake(0.05, 0.1, 0.15, 0.5));
      Fill(commands, live.ui_transmit, MTLClearColorMake(0.6, 0.6, 0.6, 0.6));
      InterpolationOutput pair;
      commands = ValidateMotion(f, live, commands);
      Check(live.Generate(commands, pair, error), error);
      const bool reset = sequence == 1 || sequence == 4;
      Check(pair.history_reset == reset, "live interpolation cut reset");
      Check(bool(pair.generated) == !reset,
            "live frame generation paired without valid history");
      Check(live.Compose(commands, live.final_scene, pair.real, error), error);
      if (pair.generated)
        Check(live.ComposeInPlace(commands, pair.generated, error), error);
      auto depth_buffer =
          [f.device newBufferWithLength:256 * input
                                options:MTLResourceStorageModeShared];
      auto blit = [commands blitCommandEncoder];
      [blit copyFromTexture:live.inputs.depth
                       sourceSlice:0
                       sourceLevel:0
                      sourceOrigin:MTLOriginMake(0, 0, 0)
                        sourceSize:MTLSizeMake(input, input, 1)
                          toBuffer:depth_buffer
                 destinationOffset:0
            destinationBytesPerRow:256
          destinationBytesPerImage:256 * input];
      [blit endEncoding];
      const auto pixels =
          f.Read(commands, pair.generated ? pair.generated : pair.real);
      for (float d :
           std::span(static_cast<const float *>(depth_buffer.contents),
                     input * input))
        Check(std::abs(d - 0.4f) < 0.00001f,
              "AA depth came from after the resolve-clear");
      for (const auto &p : pixels)
        for (size_t channel = 0; channel < 4; ++channel)
          Check(std::abs(p[channel] - Pixel{0.2f, 0.4f, 0.6f, 1}[channel]) <
                    0.01f,
                "live reconstructed/generated UI color");
      Check(live.resolved && live.ui_active && live.ui_exact,
            "live chain did not remain active");
      live.End();
      Check(!live.active && !live.ui_active, "old scene escaped live end");
    }
  }
  std::printf("live_temporal_chain=passed method=%u upscale=%u\n",
              unsigned(method), unsigned(upscaled));
}

rex::graphics::gta4_native::TemporalCommand LiveFrame(uint64_t sequence,
                                                    uint32_t size) {
  rex::graphics::gta4_native::TemporalCommand c;
  c.event = rex::graphics::gta4_native::TemporalEvent::kBeginScene;
  c.device = 1;
  c.view = 2;
  c.epoch = 1;
  c.sequence = sequence;
  c.time_ns = 1000000000 + sequence * 16666667;
  c.width = c.height = c.output_width = c.output_height = size;
  c.flags = rex::graphics::gta4_native::kTemporalJitterApplied;
  c.jitter = Jitter(sequence);
  c.aspect = 1;
  return c;
}
Pixel AnalyticScene(float u, float v, uint64_t sequence) {
  return {0.125f + 0.75f * u, 0.25f + 0.5f * v,
          0.125f + float(sequence) * 0.015625f, 1};
}
id<MTLTexture> ShiftedScene(Fixture &f, uint32_t size, uint64_t sequence,
                          std::array<float, 2> jitter) {
  auto image = f.Image(MTLPixelFormatRGBA16Float, size, size);
  std::vector<Pixel> pixels(size_t(size) * size);
  for (uint32_t y = 0; y < size; ++y)
    for (uint32_t x = 0; x < size; ++x)
      // Raster jitter moves the image by +jitter. Its sample at x therefore
      // contains the unjittered field at x-jitter, independently of CopyColor.
      pixels[size_t(y) * size + x] =
          AnalyticScene((float(x) + 0.5f - jitter[0]) / size,
                        (float(y) + 0.5f - jitter[1]) / size, sequence);
  f.WriteColor(image, pixels);
  return image;
}
enum class LiveFailure {
  kNone, kMissingDepth, kInvalidDepth, kUnjittered, kRejectedFrame, kNoComposite
};
void FallbackChain(Fixture &f, Method method, bool upscaled) {
  std::string error;
  auto context = std::make_shared<rex::ui::metal::MetalContext>();
  context->device = f.device;
  context->queue = f.queue;
  Live live;
  Check(live.Initialize(context, error), error);
  const uint32_t input = 64, output = upscaled ? 128 : input;
  const std::array failures{
      LiveFailure::kNone, LiveFailure::kNone, LiveFailure::kMissingDepth,
      LiveFailure::kNone, LiveFailure::kNone, LiveFailure::kInvalidDepth,
      LiveFailure::kNone, LiveFailure::kUnjittered, LiveFailure::kNone,
      LiveFailure::kRejectedFrame, LiveFailure::kNone,
      LiveFailure::kNoComposite, LiveFailure::kNone};
  auto depth = std::make_shared<SurfaceResource>();
  depth->image = LiveImage(f, MTLPixelFormatDepth32Float_Stencil8, input);
  depth->depth = true;
  auto wrong_depth = std::make_shared<SurfaceResource>();
  wrong_depth->image = LiveImage(f, MTLPixelFormatDepth32Float_Stencil8, 32);
  wrong_depth->depth = true;
  std::array<uint8_t, 4096> vertices{};
  std::array<uint8_t, 64> shared{};
  const GeometryKey geometry{1, 2, 3, 4, 5, 6, 7};
  bool previous_valid = false;
  for (size_t index = 0; index < failures.size(); ++index) {
    @autoreleasepool {
      const uint64_t sequence = index + 1;
      const auto failure = failures[index];
      const bool valid = failure == LiveFailure::kNone;
      const bool jittered = failure != LiveFailure::kUnjittered;
      const auto diagnostic = "fallback method=" + std::to_string(unsigned(method)) +
          " output=" + std::to_string(output) + " sequence=" +
          std::to_string(sequence) + " cause=" + std::to_string(unsigned(failure));
      auto c = LiveFrame(sequence, input);
      auto scene = ShiftedScene(f, input, sequence,
                                jittered ? c.jitter : std::array<float, 2>{});
      auto commands = [f.queue commandBuffer];
      Check(live.Begin(commands, c, method, output, output, true, error), error);
      ExposureHint(f, live, commands);
      live.jittered_draws = jittered ? 1 : 0;
      live.world_draws = failure == LiveFailure::kUnjittered ? 0 : 1;
      live.history_draws = live.world_draws;
      const auto lookup = live.geometry.Record(geometry, vertices, shared, {});
      Check(lookup.captured && bool(lookup.previous) == previous_valid,
            diagnostic + " geometry retained a failed frame");
      if (failure == LiveFailure::kNoComposite) {
        Check(live.StartUi(commands, error) && !live.ui_active && !live.HasSceneOutput(),
              diagnostic + " scheduler UI boundary reused last frame's scene");
        auto executed = std::make_shared<SurfaceResource>();
        executed->image = scene;
        Check(live.RecoverComposite(commands, executed, error), diagnostic + " " + error);
        Check(live.spatial_fallback && !live.resolved && live.composite_source == executed,
              diagnostic + " unrecognized final composite was not recovered before HUD");
      }
      if (failure != LiveFailure::kMissingDepth) {
        live.depth_source = failure == LiveFailure::kInvalidDepth ? wrong_depth : depth;
        DepthFill(commands, live.depth_source->image, 0.4);
      }
      if (failure == LiveFailure::kRejectedFrame)
        live.metadata.coverage = InputCoverage::kUnknown;
      Fill(commands, live.inputs.reactive, MTLClearColorMake(0, 0, 0, 0));
      Fill(commands, live.inputs.previous_depth, MTLClearColorMake(0.4, 0, 0, 0));
      auto image = live.Resolve(commands, scene, error);
      Check(image && error.empty(), diagnostic + " " + error);
      Check(image.width == output && image.height == output,
            diagnostic + " output extent");
      Check(live.HasSceneOutput() && live.resolved == valid &&
                live.spatial_fallback == !valid && live.composite_seen,
            diagnostic + " temporal/fallback identity");
      Check(live.Resolve(commands, scene, error) == image,
            diagnostic + " repeated resolve replaced current output");
      Check(live.StartUi(commands, error) &&
                live.ui_active == (failure == LiveFailure::kNoComposite),
            diagnostic + " HUD disagrees with executed composite recovery");
      if (valid) {
        Check(live.TemporalHistoryReset() == !previous_valid,
              diagnostic + " temporal recovery history");
      } else {
        Check(!live.fallback_reason.empty() && !live.geometry.current_entries(),
              diagnostic + " fallback did not discard temporal geometry");
      }
      auto high = live.HighComposite(MTLPixelFormatRGBA16Float, error);
      Check(bool(high), diagnostic + " " + error);
      Check(live.CopyColor(commands, image, high->image, error), error);
      Check(live.AfterComposite(commands, high->image, error), error);
      Check(live.StartUi(commands, error) && live.ui_active, error);
      // A moving, translucent HUD edge is expressed in unjittered UI pixels.
      // The analytic coverage below includes its ordinary bilinear resampling,
      // never the scene's jitter displacement.
      const uint32_t edge = sequence % 2 ? 16 : 48;
      const Pixel hud{0.875f, 0.125f, 0.5f, 1};
      std::vector<Pixel> add(size_t(input) * input), transmit(add.size());
      for (uint32_t y = 0; y < input; ++y)
        for (uint32_t x = 0; x < input; ++x) {
          const float alpha = x >= edge ? 0.5f : 0;
          const size_t pixel = size_t(y) * input + x;
          add[pixel] = {hud[0] * alpha, hud[1] * alpha, hud[2] * alpha, alpha};
          transmit[pixel] = {1 - alpha, 1 - alpha, 1 - alpha, 1};
        }
      auto ui_add = f.Image(MTLPixelFormatRGBA16Float, input, input);
      auto ui_transmit = f.Image(MTLPixelFormatRGBA16Float, input, input);
      f.WriteColor(ui_add, add);
      f.WriteColor(ui_transmit, transmit);
      Check(live.CopyColor(commands, ui_add, live.ui_add, error), error);
      Check(live.CopyColor(commands, ui_transmit, live.ui_transmit, error), error);
      InterpolationOutput pair;
      commands = ValidateMotion(f, live, commands);
      Check(live.Generate(commands, pair, error), error);
      if (valid) {
        Check(pair.real && pair.history_reset == !previous_valid &&
                  bool(pair.generated) == previous_valid,
              diagnostic + " interpolation recovery history");
      } else {
        Check(!pair.real && !pair.generated && !pair.lease,
              diagnostic + " fallback entered frame generation");
      }
      auto display = LiveImage(f, MTLPixelFormatRGBA16Float, output);
      Check(live.Compose(commands, live.final_scene, display, error), error);
      const auto pixels = f.Read(commands, display);
      if (!valid) {
        float maximum_error = 0;
        // Border texels necessarily clamp when the scene is displaced. The
        // interior has an exact linear reference independent of the shader.
        for (uint32_t y = 2; y < output - 2; ++y)
          for (uint32_t x = 2; x < output - 2; ++x) {
            const auto expected = AnalyticScene((float(x) + 0.5f) / output,
                                                 (float(y) + 0.5f) / output, sequence);
            const float alpha = 0.5f * std::clamp(
                (float(x) + 0.5f) * input / output - edge + 0.5f, 0.0f, 1.0f);
            for (size_t channel = 0; channel < 3; ++channel)
              maximum_error = std::max(maximum_error, std::abs(
                  pixels[size_t(y) * output + x][channel] -
                  (expected[channel] * (1 - alpha) + hud[channel] * alpha)));
            Check(pixels[size_t(y) * output + x][3] == 1,
                  diagnostic + " composed alpha");
          }
        std::printf("live_fallback_pixels method=%u output=%u sequence=%llu max_error=%g\n",
                    unsigned(method), output, sequence, maximum_error);
        Check(maximum_error < 0.0015f,
              diagnostic + " shifted scene/current HUD mismatch: " +
                  std::to_string(maximum_error));
      }
      live.End();
      Check(!live.HasSceneOutput() && !live.jittered_draws && !live.composite_seen,
            diagnostic + " old scene state survived End");
      previous_valid = valid;
    }
  }
  std::printf("live_fallback_recovery=passed method=%u upscale=%u\n",
              unsigned(method), unsigned(upscaled));
}

void FallbackLifetime(Fixture &f) {
  std::string error;
  auto context = std::make_shared<rex::ui::metal::MetalContext>();
  context->device = f.device;
  context->queue = f.queue;
  auto live = std::make_unique<Live>();
  Check(live->Initialize(context, error), error);
  auto wrong_queue = [f.device newCommandQueue];
  Check(!live->Begin([wrong_queue commandBuffer], LiveFrame(1, 64), Method::kTaa,
                     64, 64, false, error) && !live->active,
        "live fallback accepted unordered work from another queue");
  auto event = [f.device newSharedEvent];
  Check(event != nil, "fallback lifetime shared event");
  struct ReleaseQueue {
    id<MTLSharedEvent> event;
    ~ReleaseQueue() { event.signaledValue = 1; }
  } release{event};
  std::vector<id<MTLTexture>> snapshots;
  std::vector<id<MTLCommandBuffer>> submissions;
  const std::array<uint32_t, 4> sizes{64, 64, 128, 64};
  for (size_t index = 0; index < sizes.size(); ++index) {
    const uint64_t sequence = index + 1;
    auto c = LiveFrame(sequence, sizes[index]);
    auto source = ShiftedScene(f, sizes[index], sequence, c.jitter);
    auto commands = [f.queue commandBuffer];
    if (!index) [commands encodeWaitForEvent:event value:1];
    Check(live->Begin(commands, c, Method::kTaa, sizes[index], sizes[index], true, error),
          error);
    live->jittered_draws = 1;
    auto image = live->Resolve(commands, source, error);
    Check(image && live->spatial_fallback, error);
    auto snapshot = LiveImage(f, MTLPixelFormatRGBA16Float, sizes[index]);
    Check(live->CopyColor(commands, image, snapshot, error), error);
    snapshots.push_back(snapshot);
    live->End();
    [commands commit];
    submissions.push_back(commands);
  }
  // Reuse, resize and release while the queue is blocked. Encoded resources
  // must stay alive, and each snapshot must precede the next reuse on this queue.
  live.reset();
  Check(submissions.front().status != MTLCommandBufferStatusCompleted,
        "fallback lifetime fixture did not hold GPU work in flight");
  event.signaledValue = 1;
  for (size_t index = 0; index < snapshots.size(); ++index) {
    const auto pixels = f.Read([f.queue commandBuffer], snapshots[index]);
    const auto size = sizes[index];
    float maximum_error = 0;
    for (uint32_t y = 2; y < size - 2; ++y)
      for (uint32_t x = 2; x < size - 2; ++x) {
        const auto expected = AnalyticScene((float(x) + 0.5f) / size,
                                             (float(y) + 0.5f) / size, index + 1);
        for (size_t channel = 0; channel < 4; ++channel)
          maximum_error = std::max(maximum_error,
              std::abs(pixels[size_t(y) * size + x][channel] - expected[channel]));
      }
    Check(maximum_error < 0.001f,
          "in-flight fallback was overwritten or released: frame=" +
              std::to_string(index + 1) + " error=" + std::to_string(maximum_error));
  }
  for (auto commands : submissions)
    Check(commands.status == MTLCommandBufferStatusCompleted,
          commands.error ? commands.error.localizedDescription.UTF8String
                         : "fallback lifetime GPU failure");
  std::printf("live_fallback_inflight_reuse_resize_release=passed\n");
}
void InvalidComposite(Fixture &f) {
  std::string error;
  auto context = std::make_shared<rex::ui::metal::MetalContext>();
  context->device = f.device;
  context->queue = f.queue;
  Live live;
  Check(live.Initialize(context, error), error);
  auto commands = [f.queue commandBuffer];
  Check(live.Begin(commands, LiveFrame(1, 64), Method::kTaa, 64, 64, true, error),
        error);
  live.jittered_draws = 1;
  auto wrong_scene = ShiftedScene(f, 32, 1, {});
  Check(!live.Resolve(commands, wrong_scene, error) && !error.empty() &&
            !live.HasSceneOutput() && !live.spatial_fallback,
        "invalid scene dimensions masqueraded as a current-frame fallback");
  Check(live.StartUi(commands, error) && !live.ui_active,
        "invalid composite exposed retained scene to HUD");
  InterpolationOutput pair;
  Check(live.Generate(commands, pair, error) && !pair.real && !pair.generated,
        "invalid composite entered frame generation");
  f.Read(commands, wrong_scene);
  live.End();
  std::printf("live_invalid_composite=passed\n");
}
void RecoveryFormats(Fixture& f) {
  std::string error;
  auto context = std::make_shared<rex::ui::metal::MetalContext>();
  context->device = f.device; context->queue = f.queue;
  Live live;
  Check(live.Initialize(context, error), error);
  for (auto format : {MTLPixelFormatBGRA8Unorm, MTLPixelFormatRGBA8Unorm})
    for (uint32_t output : {64u, 128u}) {
      auto commands = [f.queue commandBuffer];
      auto c = LiveFrame(1, 64);
      Check(live.Begin(commands, c, output==64?Method::kTaa:Method::kMetalFx,
                       output, output, false, error), error);
      live.jittered_draws = 1;
      auto source = ShiftedScene(f, 64, 1, c.jitter);
      auto surface = std::make_shared<SurfaceResource>();
      surface->image = LiveImage(f, format, 64);
      Check(live.CopyColor(commands, source, surface->image, error), error);
      Check(live.RecoverComposite(commands, surface, error), error);
      Check(live.spatial_fallback && live.final_scene.width == output &&
            live.composite_source == surface && surface->initialized,
            "8-bit executed composite recovery contract");
      auto corrected_guest = LiveImage(f, MTLPixelFormatRGBA16Float, 64);
      Check(live.CopyColor(commands, surface->image, corrected_guest, error), error);
      const auto guest = f.Read(commands, corrected_guest);
      auto read = [f.queue commandBuffer];
      const auto display = f.Read(read, live.final_scene);
      for (const auto& image : {std::make_pair(64u, &guest), std::make_pair(output, &display)})
        for (uint32_t y = 2; y < image.first - 2; ++y)
          for (uint32_t x = 2; x < image.first - 2; ++x) {
            const auto expected = AnalyticScene((float(x) + 0.5f) / image.first,
                                                (float(y) + 0.5f) / image.first, 1);
            for (size_t channel = 0; channel < 4; ++channel)
              Check(std::abs((*image.second)[size_t(y) * image.first + x][channel] - expected[channel]) < 0.005f,
                    "8-bit recovery displacement/quantization mismatch");
          }
      live.End();
    }
  std::printf("live_recovery_8bit_formats=passed\n");
}
void GenerationCoverage(Fixture& f) {
  std::string error;
  auto context = std::make_shared<rex::ui::metal::MetalContext>();
  context->device = f.device; context->queue = f.queue;
  Live live;
  Check(live.Initialize(context, error), error);
  const uint32_t size = 64;
  auto scene = f.Textures(size, size);
  f.Uniform(scene, {0.25f, 0.5f, 0.75f, 1});
  auto depth = std::make_shared<SurfaceResource>();
  depth->image = LiveImage(f, MTLPixelFormatDepth32Float_Stencil8, size);
  depth->depth = true;
  enum Gap { kComplete, kMissingDraw, kOccludedMissingDraw, kHostUnknown, kReactivePixel, kInvalidPreviousDepth,
             kNotSubmitted };
  const std::array gaps{kComplete, kComplete, kMissingDraw, kComplete, kComplete,
      kHostUnknown, kComplete, kReactivePixel, kComplete, kInvalidPreviousDepth,
      kComplete, kNotSubmitted, kComplete, kComplete, kOccludedMissingDraw, kReactivePixel};
  bool previous_complete = false;
  for (size_t index = 0; index < gaps.size(); ++index) {
    const auto gap = gaps[index];
    auto commands = [f.queue commandBuffer];
    Check(live.Begin(commands, LiveFrame(index + 1, size), Method::kTaa,
                     size, size, true, error), error);
    live.world_draws = live.history_draws = live.jittered_draws = 1;
    live.motion_coverage_complete = gap != kHostUnknown;
    if (gap == kMissingDraw || gap == kOccludedMissingDraw) live.history_draws = 0;
    if (gap == kReactivePixel) live.reactive_draws = 1;
    DepthFill(commands, depth->image, 0.4);
    ++depth->content_serial;
    live.depth_source = depth;
    Fill(commands, live.inputs.reactive, MTLClearColorMake(gap == kReactivePixel ? 1 : 0, 0, 0, 0));
    Fill(commands, live.inputs.previous_depth,
         MTLClearColorMake(gap == kInvalidPreviousDepth || gap == kMissingDraw ? -1 : 0.4, 0, 0, 0));
    auto reconstructed = live.Resolve(commands, scene.color, error);
    Check(reconstructed && live.resolved, "motion gaps should not disable AA");
    auto high = live.HighComposite(MTLPixelFormatRGBA16Float, error);
    Check(bool(high), error);
    Check(live.CopyColor(commands, reconstructed, high->image, error), error);
    Check(live.AfterComposite(commands, high->image, error), error);
    Check(live.StartUi(commands, error), error);
    if (gap != kNotSubmitted) commands = ValidateMotion(f, live, commands);
    InterpolationOutput pair;
    Check(live.Generate(commands, pair, error), error);
    const bool complete = gap == kComplete || gap == kOccludedMissingDraw || gap == kReactivePixel;
    Check(bool(pair.real) == complete, "incomplete pixel motion entered interpolation history");
    Check(bool(pair.generated) == (complete && previous_complete),
          "generation did not reset across a motion gap");
    if (!complete) Check(!live.generation_skip_reason.empty(), "missing coverage diagnostic");
    [commands commit]; [commands waitUntilCompleted];
    Check(commands.status == MTLCommandBufferStatusCompleted, "coverage integration submission");
    previous_complete = complete;
    live.End();
  }
  std::printf("live_generation_motion_coverage=passed\n");
}
void LiveExposureTests(Fixture& f) {
  std::string error;
  auto context = std::make_shared<rex::ui::metal::MetalContext>();
  context->device = f.device; context->queue = f.queue;
  Live live;
  Check(live.Initialize(context, error), error);
  // Python-derived adjacent binary16 bounds for a nonrepresentable gain.
  // The R16 texture write on Apple M1 rounds this value toward zero; accepting
  // its two bracketing half values does not loosen exact clamps/singularities.
  struct Case { float adaptation, exposure, tone, expected; float minimum=expected; };
  const std::array values{
      Case{19.8799991607666f, 2.4000000953674316f, 0.699999988079071f,
           0.08453369140625f, 0.08447265625f},
      Case{1, 1, 1, 1}, Case{4, 2, 0.5f, 0.25f}, Case{0.25f, 2, 0.5f, 4},
      Case{1, 100000, 1, 65504}, Case{1, 1.0e-10f, 1, 0x1p-14f},
      Case{0, 1, 1, 65504}, Case{-1, 1, 1, 0x1p-14f},
      Case{std::numeric_limits<float>::quiet_NaN(), 1, 1, 0x1p-14f},
      Case{std::numeric_limits<float>::infinity(), 1, 1, 0x1p-14f},
      Case{-std::numeric_limits<float>::infinity(), 1, 1, 0x1p-14f}};
  uint64_t sequence = 1;
  for (const auto& value : values) {
    auto commands = [f.queue commandBuffer];
    Check(live.Begin(commands, LiveFrame(sequence++, 64), Method::kMetalFx, 64, 64, false, error), error);
    ExposureHint(f, live, commands, value.adaptation, value.exposure, value.tone);
    Check(live.inputs.exposure && live.inputs.exposure.pixelFormat == MTLPixelFormatR16Float,
          "title exposure did not produce an R16 hint");
    auto bytes = [f.device newBufferWithLength:256 options:MTLResourceStorageModeShared];
    auto copy = [commands blitCommandEncoder];
    Check(bytes && copy, "exposure readback allocation");
    [copy copyFromTexture:live.inputs.exposure sourceSlice:0 sourceLevel:0
             sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(1,1,1)
                 toBuffer:bytes destinationOffset:0 destinationBytesPerRow:256 destinationBytesPerImage:256];
    [copy endEncoding];
    [commands commit]; [commands waitUntilCompleted];
    Check(commands.status == MTLCommandBufferStatusCompleted,
          commands.error ? commands.error.localizedDescription.UTF8String : "exposure GPU failure");
    _Float16 observed; std::memcpy(&observed, bytes.contents, sizeof(observed));
    const bool within_half_bounds=float(observed)>=value.minimum&&float(observed)<=value.expected;
    if(!within_half_bounds)
      std::fprintf(stderr,"exposure adaptation=%.9g exposure=%.9g tone=%.9g expected=%.9g observed=%.9g\n",
          value.adaptation,value.exposure,value.tone,value.expected,float(observed));
    Check(within_half_bounds, "title exposure/adaptation scalar mismatch");
    live.End();
  }
  auto commands = [f.queue commandBuffer];
  auto c = LiveFrame(sequence, 64);
  Check(live.Begin(commands, c, Method::kMetalFx, 64, 64, false, error), error);
  auto integer = f.Image(MTLPixelFormatR32Uint, 1, 1);
  Check(live.SetExposure(commands, integer, 1, 1, error) && !live.inputs.exposure,
        "integer adaptation bound to a float texture shader");
  auto invalid = f.Image(MTLPixelFormatR32Float, 2, 1);
  Check(live.SetExposure(commands, invalid, 1, 1, error) && !live.inputs.exposure,
        "non-scalar adaptation was silently sampled at an invented coordinate");
  live.jittered_draws = 1;
  auto source = ShiftedScene(f, 64, sequence, c.jitter);
  auto output = live.Resolve(commands, source, error);
  Check(output && live.spatial_fallback && !live.resolved &&
        live.fallback_reason.find("exposure") != std::string::npos,
        "invalid exposure did not select current-scene recovery");
  f.Read(commands, output); live.End();
  std::printf("live_title_exposure=passed\n");
}
void ExecutionCameraContinuity(Fixture& f, Method method) {
  std::string error;
  auto context = std::make_shared<rex::ui::metal::MetalContext>();
  context->device = f.device; context->queue = f.queue;
  Live live; Check(live.Initialize(context, error), error);
  auto depth = std::make_shared<SurfaceResource>();
  depth->image = LiveImage(f, MTLPixelFormatDepth32Float_Stencil8, 64); depth->depth = true;
  auto scene = f.Textures(64,64); f.Uniform(scene, {0.25f,0.5f,0.75f,1});
  for (uint64_t sequence=1; sequence<=4; ++sequence) {
    auto commands = [f.queue commandBuffer];
    auto c = LiveFrame(sequence,64); // Scheduled normal depth, aspect 1.
    Check(live.Begin(commands,c,method,64,64,false,error),error);
    ExposureHint(f,live,commands);
    // Python-checked perspective: near1, far101, reversed depth, aspect1.5.
    live.execution_projection = {};
    live.execution_projection[0]=1; live.execution_projection[5]=1.5f;
    live.execution_projection[10]=-0.01f; live.execution_projection[11]=1;
    live.execution_projection[14]=1.01f;
    live.execution_inverse = {};
    live.execution_inverse[0]=live.execution_inverse[5]=live.execution_inverse[10]=live.execution_inverse[15]=1;
    live.execution_camera_valid=true;
    Check(live.AdoptExecutionCamera() && live.config.depth_reversed && live.metadata.aspect==1.5f,
          "execution projection did not supply depth convention/aspect");
    DepthFill(commands,depth->image,0.4); live.depth_source=depth;
    live.world_draws=live.history_draws=live.jittered_draws=1;
    Fill(commands,live.inputs.reactive,MTLClearColorMake(0,0,0,0));
    Fill(commands,live.inputs.previous_depth,MTLClearColorMake(0.4,0,0,0));
    auto output=live.Resolve(commands,scene.color,error);
    Check(output && live.resolved,error);
    Check(live.TemporalHistoryReset()==(sequence==1),
          "scheduled/execution depth mismatch recreated the temporal history");
    Check(live.AfterComposite(commands,output,error) && live.StartUi(commands,error),error);
    f.Read(commands,live.final_scene); live.End();
  }
  std::printf("live_execution_camera_continuity=passed method=%u\n",unsigned(method));
}
void BackgroundMotion(Fixture& f) {
  std::string error;
  auto context=std::make_shared<rex::ui::metal::MetalContext>();
  context->device=f.device;context->queue=f.queue;
  for(bool reversed:{false,true}) {
    Live live;Check(live.Initialize(context,error),error);
    auto depth=std::make_shared<SurfaceResource>();
    depth->image=LiveImage(f,MTLPixelFormatDepth32Float_Stencil8,64);depth->depth=true;
    auto scene=f.Textures(64,64);f.Uniform(scene,{0.25f,0.5f,0.75f,1});
    for(uint64_t sequence=1;sequence<=8;++sequence) {
      auto cb=[f.queue commandBuffer];
      auto c=LiveFrame(sequence,64);c.jitter={};
      if(sequence==8)c.flags|=rex::graphics::gta4_native::kTemporalCameraCut;
      Check(live.Begin(cb,c,Method::kTaa,64,64,false,error),error);
      live.execution_projection={1,0,0,0,0,1,0,0,0,0,reversed?-0.01f:1.01f,1,
                                  0,0,reversed?1.01f:-1.01f,0};
      // Independent Python ray rotation gives 10 degrees and no sky parallax.
      const float cosine=sequence>=3?0.984807753012208f:1.0f;
      const float sine=sequence>=3?0.17364817766693033f:0.0f;
      live.execution_inverse={cosine,0,-sine,0,0,1,0,0,sine,0,cosine,0,
                              sequence>=4?1.0f:0.0f,0,0,1};
      live.execution_camera_valid=true;Check(live.AdoptExecutionCamera(),error);
      const bool foreground=sequence==5||sequence==6;
      const bool known=sequence==6||sequence==7;
      DepthFill(cb,depth->image,foreground?0.4:(reversed?0.0:1.0));
      live.depth_source=depth;live.world_draws=live.history_draws=live.jittered_draws=1;
      if(known) {
        Fill(cb,live.inputs.motion,MTLClearColorMake(7,9,0,0));
        Fill(cb,live.inputs.previous_depth,MTLClearColorMake(0.4,0,0,0));
      }
      auto output=live.Resolve(cb,scene.color,error);Check(output&&live.resolved,error);
      Check(live.AfterComposite(cb,output,error)&&live.StartUi(cb,error),error);
      std::array<id<MTLBuffer>,2> data{};
      auto blit=[cb blitCommandEncoder];
      const std::array<id<MTLTexture>,2> textures{live.inputs.motion,live.inputs.previous_depth};
      for(size_t i=0;i<data.size();++i) {
        data[i]=[f.device newBufferWithLength:16384 options:MTLResourceStorageModeShared];
        [blit copyFromTexture:textures[i] sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
            sourceSize:MTLSizeMake(64,64,1) toBuffer:data[i] destinationOffset:0
            destinationBytesPerRow:256 destinationBytesPerImage:16384];
      }
      [blit endEncoding];[cb commit];[cb waitUntilCompleted];
      Check(cb.status==MTLCommandBufferStatusCompleted,"background motion GPU submission");
      const auto* motion=static_cast<const std::array<_Float16,2>*>(data[0].contents);
      const auto* old_depth=static_cast<const float*>(data[1].contents);
      for(size_t y=0;y<64;++y)for(size_t x=0;x<64;++x) {
        const size_t p=y*64+x;
        if(sequence==1||sequence==5||sequence==8) {
          Check(old_depth[p]==-1,"background reprojection invented foreground/first/cut correspondence");
        } else if(known) {
          Check(float(motion[p][0])==7&&float(motion[p][1])==9&&old_depth[p]==0.4f,
                "background reprojection overwrote object motion");
        } else {
          Check(old_depth[p]==(reversed?0.0f:1.0f),"background previous depth");
          const double nx=(double(x)+0.5)/64*2-1;
          const double ny=1-(double(y)+0.5)/64*2;
          const double den=-0.17364817766693033*nx+0.984807753012208;
          const double expected_x=sequence==3?((0.984807753012208*nx+0.17364817766693033)/den-nx)*32:0;
          const double expected_y=sequence==3?-(ny/den-ny)*32:0;
          Check(std::abs(float(motion[p][0])-expected_x)<0.02&&
                    std::abs(float(motion[p][1])-expected_y)<0.02,
                "background vector disagrees with analytic camera ray");
        }
      }
      live.End();
    }
  }
  std::printf("live_background_rotation_translation_cuts_and_object_preservation=passed\n");
}
void DepthOnlyMotion(Fixture& f) {
  std::string error;
  auto context=std::make_shared<rex::ui::metal::MetalContext>();
  context->device=f.device;context->queue=f.queue;
  Live live;Check(live.Initialize(context,error),error);
  NSString* source=@R"(
    #include <metal_stdlib>
    using namespace metal;
    struct Out {float4 position [[position]];float4 current [[user(LIBERTY_CURRENT_CLIP)]];
      float4 previous [[user(LIBERTY_PREVIOUS_CLIP)]];};
    vertex Out fixture(uint id [[vertex_id]]) {
      const float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};
      float4 current=float4(p[id],0.5f,1);
      return {current,current,current+float4(0.25f,-0.125f,-0.2f,0)};
    })";
  NSError* native_error=nil;
  auto library=[f.device newLibraryWithSource:source options:nil error:&native_error];
  Check(library!=nil,native_error?native_error.localizedDescription.UTF8String:"depth-motion fixture");
  auto pd=[MTLRenderPipelineDescriptor new];
  pd.vertexFunction=[library newFunctionWithName:@"fixture"];
  pd.fragmentFunction=live.DepthMotionFunction();
  pd.colorAttachments[4].pixelFormat=MTLPixelFormatRG16Float;
  pd.colorAttachments[5].pixelFormat=MTLPixelFormatR8Unorm;
  pd.colorAttachments[6].pixelFormat=MTLPixelFormatR32Float;
  auto pipeline=[f.device newRenderPipelineStateWithDescriptor:pd error:&native_error];
  Check(pipeline!=nil,native_error?native_error.localizedDescription.UTF8String:"depth-motion pipeline");
  for(bool valid:{false,true}) {
    auto cb=[f.queue commandBuffer];
    Check(live.Begin(cb,LiveFrame(valid?2:1,64),Method::kTaa,64,64,false,error),error);
    auto pass=[MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[4].texture=live.inputs.motion;
    pass.colorAttachments[5].texture=live.inputs.reactive;
    pass.colorAttachments[6].texture=live.inputs.previous_depth;
    for(size_t i:{4u,5u,6u}){
      pass.colorAttachments[i].loadAction=MTLLoadActionLoad;
      pass.colorAttachments[i].storeAction=MTLStoreActionStore;
    }
    auto e=[cb renderCommandEncoderWithDescriptor:pass];
    DrawParameters parameters{};parameters.input_extent={64,64};parameters.valid_history=valid;
    [e setRenderPipelineState:pipeline];[e setFragmentBytes:&parameters length:sizeof(parameters) atIndex:6];
    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[e endEncoding];
    auto bytes=[f.device newBufferWithLength:49152 options:MTLResourceStorageModeShared];
    auto blit=[cb blitCommandEncoder];
    [blit copyFromTexture:live.inputs.previous_depth sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
        sourceSize:MTLSizeMake(64,64,1) toBuffer:bytes destinationOffset:0
        destinationBytesPerRow:256 destinationBytesPerImage:16384];
    [blit copyFromTexture:live.inputs.motion sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
        sourceSize:MTLSizeMake(64,64,1) toBuffer:bytes destinationOffset:16384
        destinationBytesPerRow:256 destinationBytesPerImage:16384];
    [blit copyFromTexture:live.inputs.reactive sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
        sourceSize:MTLSizeMake(64,64,1) toBuffer:bytes destinationOffset:32768
        destinationBytesPerRow:256 destinationBytesPerImage:16384];
    [blit endEncoding];[cb commit];[cb waitUntilCompleted];
    Check(cb.status==MTLCommandBufferStatusCompleted,"depth-motion GPU submission");
    const auto* previous=static_cast<const float*>(bytes.contents);
    const auto* raw=static_cast<const uint8_t*>(bytes.contents);
    const auto* motion=reinterpret_cast<const _Float16(*)[2]>(raw+16384);
    for(size_t i=0;i<4096;++i) {
      Check(std::abs(previous[i]-(valid?0.3f:-1.0f))<0.00001f,"depth-only geometry correspondence");
      // Independent interpolators can put the nominally exact delta just below
      // its float value. M1 render-target conversion then rounds to the adjacent
      // lower half. Bounds are the Python-derived immediate binary16 neighbors;
      // unknown history still requires exact zero rather than a tolerance.
      const float x=float(motion[i][0]),y=float(motion[i][1]);
      const bool correct_motion=valid?
          x>=7.99609375f&&x<=8.0078125f&&y>=3.998046875f&&y<=4.00390625f:
          x==0&&y==0;
      if(!correct_motion)
        std::fprintf(stderr,"depth_motion valid=%d pixel=%zu observed=(%.9g,%.9g) previous_depth=%.9g\n",
            valid,i,float(motion[i][0]),float(motion[i][1]),previous[i]);
      Check(correct_motion,"depth-only current-to-previous motion");
      Check(raw[32768+(i/64)*256+i%64]==(valid?0:255),"depth-only reactive coverage");
    }
    live.End();
  }
  std::printf("live_depth_only_geometry_motion=passed\n");
}
} // namespace
int main(int argc, char **argv) {
  @autoreleasepool {
    try {
      Check(argc == 2, "usage: temporal-live-test temporal.metallib");
      CameraTests();
      Fixture fixture(argv[1]);
      LiveChain(fixture, Method::kTaa, false);
      LiveChain(fixture, Method::kMetalFx, false);
      LiveChain(fixture, Method::kMetalFx, true);
      FallbackChain(fixture, Method::kTaa, false);
      FallbackChain(fixture, Method::kMetalFx, false);
      FallbackChain(fixture, Method::kMetalFx, true);
      FallbackLifetime(fixture);
      InvalidComposite(fixture);
      RecoveryFormats(fixture);
      GenerationCoverage(fixture);
      ExecutionCameraContinuity(fixture, Method::kTaa);
      ExecutionCameraContinuity(fixture, Method::kMetalFx);
      BackgroundMotion(fixture);
      DepthOnlyMotion(fixture);
      LiveExposureTests(fixture);
      std::printf("metal_temporal_live_gpu=passed cases=%zu checks=%zu\n",
                  cases, checks);
      return 0;
    } catch (const std::exception &e) {
      std::fprintf(stderr, "Live temporal test: %s\n", e.what());
      return 1;
    }
  }
}
