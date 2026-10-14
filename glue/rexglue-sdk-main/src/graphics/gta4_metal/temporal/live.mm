#include "live.h"
#include <rex/logging.h>
#include <algorithm>
#include <cmath>
#include "projection.h"
namespace rex::graphics::gta4_metal::temporal {
namespace {
void Dispatch(id<MTLComputeCommandEncoder> e, id<MTLComputePipelineState> p, NSUInteger w,
              NSUInteger h) {
  const NSUInteger x = p.threadExecutionWidth,
                   y = std::max<NSUInteger>(
                       1, std::min<NSUInteger>(8, p.maxTotalThreadsPerThreadgroup / x));
  [e dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:MTLSizeMake(x, y, 1)];
}
}  // namespace
bool Live::Initialize(std::shared_ptr<ui::metal::MetalContext> c, std::string& error) {
  context_ = std::move(c);
  library_ = CreateLibrary(context_->device, error);
  if (!library_) return false;
  depth_motion_fragment_=[library_ newFunctionWithName:@"liberty_temporal_depth_motion"];
  if(!depth_motion_fragment_){error="depth-only temporal motion fragment is absent";return false;}
  auto pipeline = [&](NSString* name, __strong id<MTLComputePipelineState>& p) {
    NSError* e = nil;
    auto f = [library_ newFunctionWithName:name];
    if (f) p = [context_->device newComputePipelineStateWithFunction:f error:&e];
    if (!p) error = e ? e.localizedDescription.UTF8String : "temporal utility shader is absent";
    return p != nil;
  };
  return pipeline(@"liberty_temporal_export_depth", depth_export_) &&
         pipeline(@"liberty_temporal_copy_color", color_copy_) &&
         pipeline(@"liberty_temporal_exposure_hint", exposure_hint_) &&
         pipeline(@"liberty_temporal_background_motion", background_motion_) &&
         pipeline(@"liberty_temporal_affine_ui", compose_) &&
         pipeline(@"liberty_temporal_affine_ui_inplace", compose_inplace_);
}
bool Live::CreateImage(__strong id<MTLTexture>& image, MTLPixelFormat format, uint32_t w,
                       uint32_t h, NSString* name, std::string& error) {
  if (image && image.width == w && image.height == h && image.pixelFormat == format) return true;
  if (!w || !h || uint64_t(w) * h > uint64_t(4096) * 4096) {
    error = "temporal live image extent exceeds budget";
    return false;
  }
  auto d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                              width:w
                                                             height:h
                                                          mipmapped:NO];
  d.storageMode = MTLStorageModePrivate;
  d.hazardTrackingMode = MTLHazardTrackingModeTracked;
  d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
  auto next = [context_->device newTextureWithDescriptor:d];
  if (!next) {
    error = "temporal live texture allocation failed";
    return false;
  }
  next.label = name;
  image = next;
  return true;
}
bool Live::Clear(id<MTLCommandBuffer> cb, id<MTLTexture> image, double value, std::string& error) {
  auto p = [MTLRenderPassDescriptor renderPassDescriptor];
  p.colorAttachments[0].texture = image;
  p.colorAttachments[0].loadAction = MTLLoadActionClear;
  p.colorAttachments[0].storeAction = MTLStoreActionStore;
  p.colorAttachments[0].clearColor = MTLClearColorMake(value, value, value, value);
  auto e = [cb renderCommandEncoderWithDescriptor:p];
  if (!e) {
    error = "temporal attachment clear failed";
    return false;
  }
  e.label = @"Liberty temporal attachment initialization";
  [e endEncoding];
  return true;
}
bool Live::Begin(id<MTLCommandBuffer> cb, const gta4_native::TemporalCommand& c, Method method,
                 uint32_t ow, uint32_t oh, bool generation, std::string& error) {
  if (!context_ || !cb || cb.commandQueue != context_->queue) {
    error = "temporal live scene must use its renderer command queue";
    return false;
  }
  End();
  if (last_time_ns_ && c.time_ns > last_time_ns_ && c.time_ns - last_time_ns_ <= 250000000 &&
      last_epoch_ == c.epoch && last_view_ == c.view &&
      !(c.flags & gta4_native::kTemporalCameraCut)) {
    const uint64_t measured = c.time_ns - last_time_ns_;
    smoothed_interval_ns_ =
        smoothed_interval_ns_ ? (7 * smoothed_interval_ns_ + measured) / 8 : measured;
  } else
    smoothed_interval_ns_ = 0;
  last_time_ns_ = c.time_ns;
  last_epoch_ = c.epoch;
  last_view_ = c.view;
  presentation_interval_ns = smoothed_interval_ns_ / 2;
  config = {c.width,
            c.height,
            ow ? ow : c.width,
            oh ? oh : c.height,
            method,
            bool(c.flags & gta4_native::kTemporalDepthReversed)};
  if (method == Method::kTaa) {
    config.output_width = config.input_width;
    config.output_height = config.input_height;
  }
  metadata = {};
  metadata.sequence = c.sequence;
  metadata.epoch = c.epoch;
  metadata.view = c.view;
  metadata.time_ns = c.time_ns;
  metadata.jitter = c.jitter;
  metadata.near_plane = c.near_plane;
  metadata.far_plane = c.far_plane;
  metadata.field_of_view = c.field_of_view;
  metadata.aspect = c.aspect;
  metadata.coverage = InputCoverage::kReactiveSceneMotion;
  metadata.color_domain = ColorDomain::kSceneLinear;
  metadata.scene_without_ui = true;
  metadata.jitter_applied = c.flags & gta4_native::kTemporalJitterApplied;
  metadata.camera_cut = c.flags & gta4_native::kTemporalCameraCut;
  if (!ValidConfiguration(config) ||
      !geometry.Begin(c.epoch, c.view, c.sequence, metadata.camera_cut)) {
    error = "temporal scene identity or dimensions invalid";
    return false;
  }
  if (!CreateImage(inputs.motion, MTLPixelFormatRG16Float, c.width, c.height,
                   @"Liberty scene motion vectors", error) ||
      !CreateImage(inputs.reactive, MTLPixelFormatR8Unorm, c.width, c.height,
                   @"Liberty reactive mask", error) ||
      !CreateImage(inputs.depth, MTLPixelFormatR32Float, c.width, c.height,
                   @"Liberty temporal device depth", error) ||
      !CreateImage(inputs.previous_depth, MTLPixelFormatR32Float, c.width, c.height,
                   @"Liberty expected previous surface depth", error) ||
      !CreateImage(converted_, MTLPixelFormatRGBA16Float, c.width, c.height,
                   @"Liberty linear temporal input", error) ||
      !CreateImage(ui_add, MTLPixelFormatRGBA16Float, c.width, c.height,
                   @"Liberty UI additive component", error) ||
      !CreateImage(ui_transmit, MTLPixelFormatRGBA16Float, c.width, c.height,
                   @"Liberty UI scene transmission", error))
    return false;
  // Configure only at Resolve, after execution has supplied the real camera.
  // Configuring from scheduler data here can recreate the scaler each frame
  // if its depth convention differs from the subsequently adopted camera.
  if (!Clear(cb, inputs.motion, 0, error) || !Clear(cb, inputs.reactive, 1, error) ||
      !Clear(cb, inputs.previous_depth, -1, error)) return false;
  frame_generation = generation;
  active = true;
  return true;
}
bool Live::CaptureDepth(id<MTLCommandBuffer> cb, std::string& error) {
  if (!active || !depth_source || !depth_source->image) return true;
  auto source = depth_source->image;
  if (source.width != inputs.depth.width || source.height != inputs.depth.height ||
      source.sampleCount != 1) {
    error = "temporal depth is not the current single-sample scene";
    return false;
  }
  // A resolve may clear this guest attachment after exporting it. Preserve
  // the pre-clear snapshot until a later actual scene depth write invalidates it.
  if (depth_captured) return true;
  auto e = [cb computeCommandEncoder];
  if (!e) {
    error = "temporal depth export encoder failed";
    return false;
  }
  e.label = @"Liberty snapshot of actual scene depth";
  [e setComputePipelineState:depth_export_];
  [e setTexture:source atIndex:0];
  [e setTexture:inputs.depth atIndex:1];
  Dispatch(e, depth_export_, source.width, source.height);
  [e endEncoding];
  depth_captured = true;
  last_depth_serial = depth_source->content_serial;
  return true;
}
bool Live::CopyColor(id<MTLCommandBuffer> cb, id<MTLTexture> source, id<MTLTexture> dest,
                     std::string& error, std::array<float, 2> offset) {
  if (!source || !dest || source == dest) {
    error = "invalid temporal color copy";
    return false;
  }
  auto e = [cb computeCommandEncoder];
  if (!e) {
    error = "temporal color copy encoder failed";
    return false;
  }
  e.label = @"Liberty temporal color conversion";
  [e setComputePipelineState:color_copy_];
  [e setTexture:source atIndex:0];
  [e setTexture:dest atIndex:1];
  [e setBytes:offset.data() length:sizeof(offset) atIndex:0];
  Dispatch(e, color_copy_, dest.width, dest.height);
  [e endEncoding];
  return true;
}
bool Live::CompleteBackgroundMotion(id<MTLCommandBuffer> cb, std::string& error) {
  if (!adopted_camera || !background_history_valid_ || metadata.camera_cut) return true;
  struct Parameters {
    std::array<float,16> transform;
    std::array<float,2> jitter, half_pixel;
    uint32_t width, height, reversed, padding;
  };
  static_assert(sizeof(Parameters)==96);
  const Parameters p{background_transform_,metadata.jitter,camera_half_pixel,
                      config.input_width,config.input_height,config.depth_reversed,0};
  auto e=[cb computeCommandEncoder];
  if(!e){error="background motion encoder creation failed";return false;}
  e.label=@"Liberty infinite-background camera motion";
  [e setComputePipelineState:background_motion_];
  [e setTexture:inputs.depth atIndex:0];
  [e setTexture:inputs.motion atIndex:1];
  [e setTexture:inputs.previous_depth atIndex:2];
  [e setBytes:&p length:sizeof(p) atIndex:0];
  Dispatch(e,background_motion_,config.input_width,config.input_height);
  [e endEncoding];
  return true;
}
bool Live::SetExposure(id<MTLCommandBuffer> cb, id<MTLTexture> adaptation, float exposure,
                       float tone_scale, std::string& error) {
  inputs.exposure = nil;
  if (config.method != Method::kMetalFx) return true;
  const bool float_readable = adaptation && ([&] {
    switch (adaptation.pixelFormat) {
      case MTLPixelFormatR8Unorm: case MTLPixelFormatRG8Unorm:
      case MTLPixelFormatRGBA8Unorm: case MTLPixelFormatBGRA8Unorm:
      case MTLPixelFormatR16Float: case MTLPixelFormatRG16Float:
      case MTLPixelFormatRGBA16Float: case MTLPixelFormatR32Float:
      case MTLPixelFormatRG32Float: case MTLPixelFormatRGBA32Float:
      case MTLPixelFormatRGB10A2Unorm: return true;
      default: return false;
    }
  })();
  // A 1x1 input makes the title's sampler coordinate/filter immaterial. Other
  // layouts require an explicit contract, rather than sampling an invented UV.
  if (!float_readable || adaptation.device != context_->device ||
      adaptation.textureType != MTLTextureType2D || adaptation.sampleCount != 1 ||
      adaptation.width != 1 || adaptation.height != 1 ||
      !(adaptation.usage & MTLTextureUsageShaderRead) ||
      !std::isfinite(exposure) || exposure <= 0 ||
      !std::isfinite(tone_scale) || tone_scale <= 0) {
    // Resolve will produce a current-scene fallback if the title cannot
    // supply a supported quality hint. No host readback or render-thread wait.
    return true;
  }
  if (!CreateImage(exposure_, MTLPixelFormatR16Float, 1, 1,
                   @"Liberty title-adapted MetalFX exposure", error)) return false;
  auto e = [cb computeCommandEncoder];
  if (!e) { error = "temporal exposure encoder creation failed"; return false; }
  e.label = @"Liberty exposure from title adaptation";
  const std::array<float, 2> factors{exposure, tone_scale};
  [e setComputePipelineState:exposure_hint_];
  [e setTexture:adaptation atIndex:0];
  [e setTexture:exposure_ atIndex:1];
  [e setBytes:factors.data() length:sizeof(factors) atIndex:0];
  Dispatch(e, exposure_hint_, 1, 1);
  [e endEncoding];
  inputs.exposure = exposure_;
  return true;
}
id<MTLTexture> Live::Resolve(id<MTLCommandBuffer> cb, id<MTLTexture> scene, std::string& error) {
  if (!active) return nil;
  if (resolved) return resolved_.color;
  if (spatial_fallback) return spatial_output_;
  composite_seen = true;
  if (!scene || scene.textureType != MTLTextureType2D || scene.sampleCount != 1 ||
      scene.width != config.input_width || scene.height != config.input_height) {
    ResetHistory();
    error = "temporal composite has no current single-sample scene color";
    return nil;
  }
  // Metadata describes the planned jitter. Only emitted scene draws establish
  // that it actually reached this image; menus and skipped draws must not move.
  if (!jittered_draws) {
    metadata.jitter = {};
    metadata.jitter_applied = false;
  }
  if (config.method == Method::kMetalFx && !inputs.exposure) {
    error = "temporal scene exposure/adaptation contract is unavailable";
  } else if (!depth_source || !depth_source->image || !world_draws) {
    error = "temporal scene depth/motion is incomplete";
  } else if (CaptureDepth(cb, error) && CompleteBackgroundMotion(cb, error) &&
             CopyColor(cb, scene, converted_, error)) {
    inputs.color = converted_;
    if (aa_.Configure(context_->device, library_, config, OwnedTextureBudget(context_->device), error) &&
        aa_.Encode(cb, inputs, metadata, resolved_, error)) {
      KeepUntilCompleted(cb, resolved_.lease);
      resolved = true;
      return resolved_.color;
    }
  }
  fallback_reason = error;
  ResetHistory();
  resolved_ = {};
  // This boundary still has scene-only color. Compensate raster jitter while
  // resizing the CURRENT scene, then let the original composite and HUD run.
  // A previous temporal image would freeze scene motion under a fresh HUD.
  if (!std::isfinite(metadata.jitter[0]) || !std::isfinite(metadata.jitter[1])) {
    error = "temporal scene jitter is not finite for spatial fallback";
    return nil;
  }
  if (!CreateImage(spatial_output_, MTLPixelFormatRGBA16Float, config.output_width,
                   config.output_height, @"Liberty current scene spatial fallback", error) ||
      !CopyColor(cb, scene, spatial_output_, error, metadata.jitter))
    return nil;
  spatial_fallback = true;
  error.clear();
  return spatial_output_;
}
std::shared_ptr<SurfaceResource> Live::HighComposite(MTLPixelFormat format, std::string& error) {
  if (!active || !HasSceneOutput()) return {};
  if (!high_composite_) high_composite_ = std::make_shared<SurfaceResource>();
  if (!CreateImage(high_composite_->image, format, config.output_width, config.output_height,
                   @"Liberty postprocessing at MetalFX output resolution", error))
    return {};
  high_composite_->descriptor.width = config.input_width;
  high_composite_->descriptor.height = config.input_height;
  high_composite_->initialized = false;
  return high_composite_;
}
std::shared_ptr<SurfaceResource> Live::FilterComposite(std::string& error) {
  if (!active) {
    error = "stock temporal filter has no active scene";
    return {};
  }
  if (!filtered_composite_) filtered_composite_ = std::make_shared<SurfaceResource>();
  // Keep the exact stock filter in float32 until the AA input conversion. The
  // original HDR and discrete GBuffer selector are sampled only on their input
  // grid. This private tracked image is retained by every encoded command.
  if (!CreateImage(filtered_composite_->image, MTLPixelFormatRGBA32Float,
                   config.input_width, config.input_height,
                   @"Liberty stock scene-linear composite filter", error))
    return {};
  filtered_composite_->descriptor.width = config.input_width;
  filtered_composite_->descriptor.height = config.input_height;
  filtered_composite_->initialized = false;
  return filtered_composite_;
}
bool Live::AfterComposite(id<MTLCommandBuffer> cb, id<MTLTexture> scene, std::string& error) {
  if (!active || !HasSceneOutput() || !scene) return true;
  if (!CreateImage(final_scene, MTLPixelFormatRGBA16Float, config.output_width,
                   config.output_height, @"Liberty reconstructed scene without HUD", error))
    return false;
  composite_ready_ = CopyColor(cb, scene, final_scene, error);
  return composite_ready_;
}
bool Live::RecoverComposite(id<MTLCommandBuffer> cb,
                            const std::shared_ptr<SurfaceResource>& surface,
                            std::string& error) {
  if (!active || HasSceneOutput()) return true;
  if (!surface || !surface->image || surface->image.textureType != MTLTextureType2D ||
      surface->image.sampleCount != 1 || surface->image.width != config.input_width ||
      surface->image.height != config.input_height || ui_active) {
    error = "executed final composite is not an intact scene-only color target";
    return false;
  }
  const auto offset = jittered_draws ? metadata.jitter : std::array<float, 2>{};
  for (const auto value : offset) {
    if (!std::isfinite(value)) {
      error = "executed final composite has invalid raster jitter";
      return false;
    }
  }
  auto source = surface->image;
  // The final title composite has executed synchronously, but HUD has not.
  // Recover only the current postprocessed scene. This is spatial recovery,
  // never an assertion that tone-mapped input is valid temporal scene HDR.
  if (!CreateImage(spatial_output_, MTLPixelFormatRGBA16Float, config.output_width,
                   config.output_height, @"Liberty executed composite recovery", error) ||
      !CreateImage(recovered_composite_, source.pixelFormat, config.input_width,
                   config.input_height, @"Liberty recovered guest composite", error) ||
      !CopyColor(cb, source, spatial_output_, error, offset) ||
      !CopyColor(cb, source, recovered_composite_, error, offset)) return false;
  // Keep the guest target coherent too, including for HUD blend modes which
  // cannot be represented by our optional separated UI path. A blit requires
  // no shader-write usage on the original render target.
  auto copy = [cb blitCommandEncoder];
  if (!copy) { error = "executed composite recovery copy failed"; return false; }
  copy.label = @"Liberty scene recovery before original HUD";
  [copy copyFromTexture:recovered_composite_ sourceSlice:0 sourceLevel:0
           sourceOrigin:MTLOriginMake(0, 0, 0)
             sourceSize:MTLSizeMake(source.width, source.height, 1)
              toTexture:source destinationSlice:0 destinationLevel:0
      destinationOrigin:MTLOriginMake(0, 0, 0)];
  [copy endEncoding];
  ++surface->content_serial;
  surface->initialized = true;
  ResetHistory();
  resolved_ = {};
  spatial_fallback = composite_seen = composite_ready_ = true;
  final_scene = spatial_output_;
  composite_source = surface;
  motion_coverage_complete = false;
  fallback_reason = "unrecognized executed final composite";
  error.clear();
  return true;
}
bool Live::StartUi(id<MTLCommandBuffer> cb, std::string& error) {
  if (ui_active || !active || !HasSceneOutput() || !composite_ready_ || !final_scene) return true;
  if (!Clear(cb, ui_add, 0, error) || !Clear(cb, ui_transmit, 1, error)) return false;
  ui_active = true;
  return true;
}
bool Live::Compose(id<MTLCommandBuffer> cb, id<MTLTexture> scene, id<MTLTexture> dest,
                   std::string& error) {
  if (!scene || !dest || !ui_active) return false;
  auto e = [cb computeCommandEncoder];
  if (!e) {
    error = "temporal UI composition encoder failed";
    return false;
  }
  e.label = @"Liberty current HUD after temporal reconstruction";
  [e setComputePipelineState:compose_];
  [e setTexture:scene atIndex:0];
  [e setTexture:ui_add atIndex:1];
  [e setTexture:ui_transmit atIndex:2];
  [e setTexture:dest atIndex:3];
  Dispatch(e, compose_, dest.width, dest.height);
  [e endEncoding];
  return true;
}
id<MTLTexture> Live::ResampleHalf(id<MTLCommandBuffer> cb, id<MTLTexture> source,
                                  std::string& error) {
  if (!source ||
      (source.width == config.output_width / 2 && source.height == config.output_height / 2))
    return source;
  if (!CreateImage(half_scene_, MTLPixelFormatRGBA16Float, config.output_width / 2,
                   config.output_height / 2, @"Liberty DoF half scene at output resolution",
                   error) ||
      !CopyColor(cb, source, half_scene_, error))
    return nil;
  return half_scene_;
}
bool Live::ComposeInPlace(id<MTLCommandBuffer> cb, id<MTLTexture> image, std::string& error) {
  auto e = [cb computeCommandEncoder];
  if (!e) {
    error = "generated HUD composition encoder failed";
    return false;
  }
  e.label = @"Liberty HUD on generated frame";
  [e setComputePipelineState:compose_inplace_];
  [e setTexture:image atIndex:0];
  [e setTexture:ui_add atIndex:1];
  [e setTexture:ui_transmit atIndex:2];
  Dispatch(e, compose_inplace_, image.width, image.height);
  [e endEncoding];
  return true;
}
bool Live::NeedsMotionCoverage() const {
  return active && frame_generation && resolved && final_scene && ui_active && ui_exact &&
         motion_coverage_complete && world_draws;
}
bool Live::ValidateMotionCoverage(id<MTLCommandBuffer> cb, std::string& error) {
  motion_coverage_.Invalidate();
  return !NeedsMotionCoverage() || motion_coverage_.Encode(cb, inputs, library_, error);
}
bool Live::Generate(id<MTLCommandBuffer> cb, InterpolationOutput& output, std::string& error) {
  output = {};
  generation_skip_reason.clear();
  if (!active || !frame_generation || !resolved || !final_scene || !ui_active || !ui_exact) {
    interpolation_.Reset();
    return true;
  }
  if (!motion_coverage_complete || !world_draws) {
    // A zero vector denotes a stationary point. Missing object history must
    // not be forwarded as zero motion to an effect without a reactive input.
    generation_skip_reason = "untracked scene motion or camera coverage is unavailable";
    interpolation_.Reset();
    return true;
  }
  if (!motion_coverage_.Complete()) {
    generation_skip_reason = "complete per-pixel motion is unavailable";
    interpolation_.Reset();
    return true;
  }
  if (!interpolation_.Configure(context_->device, library_, config, OwnedTextureBudget(context_->device), error))
    return false;
  Frame f = metadata;
  f.coverage = InputCoverage::kSceneMotion;
  f.color_domain = ColorDomain::kDisplayPerceptual;
  Inputs in = inputs;
  in.color = final_scene;
  return interpolation_.Encode(cb, in, {}, f, output, error);
}
bool Live::AdoptExecutionCamera() {
  if (!active || !execution_camera_valid) return false;
  const auto camera = DescribeProjection(execution_projection);
  if (!camera) return false;
  if (!adopted_camera) {
    actual_projection = execution_projection;
    actual_inverse = execution_inverse;
    if (previous_camera_valid_ && CameraDiscontinuity(previous_camera_, actual_inverse))
      metadata.camera_cut = true;
    metadata.near_plane = camera->near_plane;
    metadata.far_plane = camera->far_plane;
    metadata.field_of_view = camera->field_of_view;
    metadata.aspect = camera->aspect;
    config.depth_reversed = camera->reversed;
    const auto continuity=camera_history_.Inspect(metadata,true);
    if(previous_camera_valid_&&continuity.accepted&&!continuity.reset){
      const auto transform=BackgroundReprojection(actual_projection,actual_inverse,
                                                   previous_projection_,previous_camera_);
      background_history_valid_=transform.has_value();
      if(transform)background_transform_=*transform;
    }
    previous_camera_=actual_inverse;
    previous_projection_=actual_projection;
    previous_camera_valid_=true;
    if(continuity.accepted)camera_history_.Commit(metadata);
    else camera_history_.Reset();
    adopted_camera = true;
  }
  return IsExecutionCamera();
}
bool Live::IsExecutionCamera() const {
  if (!adopted_camera || !execution_camera_valid) return false;
  for (size_t i = 0; i < 16; ++i) {
    if (std::abs(actual_projection[i] - execution_projection[i]) >
        0.0001f *
            std::max({1.0f, std::abs(actual_projection[i]), std::abs(execution_projection[i])}))
      return false;
    if (std::abs(actual_inverse[i] - execution_inverse[i]) >
        0.0001f * std::max({1.0f, std::abs(actual_inverse[i]), std::abs(execution_inverse[i])}))
      return false;
  }
  return true;
}
void Live::Reset() {
  End();
  ResetHistory();
  previous_camera_valid_ = false;
  last_time_ns_ = last_epoch_ = last_view_ = smoothed_interval_ns_ = 0;
}
void Live::ResetHistory() {
  aa_.Reset();
  interpolation_.Reset();
  geometry.Reset();
  camera_history_.Reset();
  background_history_valid_=false;
}
void Live::End() {
  motion_coverage_.Invalidate();
  // A scene that never reached a recognized composite cannot safely be
  // repaired after its HUD. Discard its history without moving the final image.
  if (active && jittered_draws && !composite_ready_) {
    ++uncovered_frames_;
    if (uncovered_frames_ <= 4 || uncovered_frames_ % 60 == 0)
      REXLOG_WARN("gta4-metal-temporal-uncovered frame={} jittered={} composite={} total={} reason=no-scene-output-before-HUD",
                  metadata.sequence, jittered_draws, composite_seen, uncovered_frames_);
  }
  if (active && (!resolved || !composite_ready_)) ResetHistory();
  active = ui_active = resolved = depth_captured = adopted_camera = execution_camera_valid = false;
  spatial_fallback = composite_seen = composite_ready_ = false;
  background_history_valid_=false;
  camera_half_pixel={};
  composite_shader = 0;
  composite_scene_stage = 0;
  fallback_reason.clear();
  generation_skip_reason.clear();
  motion_coverage_complete = true;
  jitter_decided = jitter_eligible = false;
  inputs.exposure = nil;
  ui_exact = true;
  main_view = false;
  execution_scene_geometry = execution_screen_space = execution_attributed = false;
  execution_composite = execution_final_composite = false;
  execution_composite_scope = active_composite_scope = 0;
  diagnostic_depth_draws = 0;
  diagnostic_missing_motion = 0;
  world_draws = history_draws = reactive_draws = ui_draws = jittered_draws = 0;
  instance = drawable = pose = 0;
  last_depth_serial = 0;
  resolved_ = {};
  depth_source.reset();
  composite_source.reset();
  executed_composite_source.reset();
}
}  // namespace rex::graphics::gta4_metal::temporal
