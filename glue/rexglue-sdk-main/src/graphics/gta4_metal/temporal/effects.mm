#include "effects.h"
#import <MetalFX/MetalFX.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <utility>

namespace rex::graphics::gta4_metal::temporal {
namespace {
constexpr size_t kMaximumOwnedBytes = 1024u * 1024u * 1024u;
struct FailureState {
  std::atomic<uint64_t> count{0};
};
struct LeasedImages {
  id<MTLTexture> first = nil, second = nil;
};
void ObserveFailure(id<MTLCommandBuffer> commands, const std::shared_ptr<FailureState>& state) {
  [commands addCompletedHandler:^(id<MTLCommandBuffer> completed) {
    if (completed.status == MTLCommandBufferStatusError)
      state->count.fetch_add(1, std::memory_order_release);
  }];
}
bool Available(id<MTLCommandBuffer> owner) {
  return !owner || owner.status >= MTLCommandBufferStatusCompleted;
}
bool TextureMatches(id<MTLTexture> texture, id<MTLDevice> device, MTLPixelFormat format,
                    NSUInteger width, NSUInteger height, MTLTextureUsage usage) {
  return texture && texture.device == device && texture.textureType == MTLTextureType2D &&
         texture.pixelFormat == format && texture.width == width && texture.height == height &&
         texture.sampleCount == 1 && (texture.usage & usage) == usage &&
         texture.storageMode != MTLStorageModeMemoryless &&
         texture.hazardTrackingMode == MTLHazardTrackingModeTracked;
}
size_t AllocationSize(id<MTLDevice> device, MTLPixelFormat format, NSUInteger width,
                      NSUInteger height, MTLTextureUsage usage) {
  auto descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                                       width:width
                                                                      height:height
                                                                   mipmapped:NO];
  descriptor.storageMode = MTLStorageModePrivate;
  descriptor.hazardTrackingMode = MTLHazardTrackingModeTracked;
  descriptor.usage = usage;
  return [device heapTextureSizeAndAlignWithDescriptor:descriptor].size;
}
id<MTLTexture> Allocate(id<MTLDevice> device, MTLPixelFormat format, NSUInteger width,
                        NSUInteger height, MTLTextureUsage usage, NSString* label, size_t limit,
                        size_t& owned, std::string& error) {
  auto descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                                       width:width
                                                                      height:height
                                                                   mipmapped:NO];
  descriptor.storageMode = MTLStorageModePrivate;
  descriptor.hazardTrackingMode = MTLHazardTrackingModeTracked;
  descriptor.usage = usage;
  const auto predicted = [device heapTextureSizeAndAlignWithDescriptor:descriptor].size;
  if (owned > limit || predicted > limit - owned) {
    error = "temporal owned-texture budget exceeded";
    return nil;
  }
  auto image = [device newTextureWithDescriptor:descriptor];
  if (!image) {
    error = "temporal texture allocation failed";
    return nil;
  }
  if (image.allocatedSize > limit - owned) {
    error = "temporal allocation exceeds its budget";
    return nil;
  }
  owned += image.allocatedSize;
  image.label = label;
  return image;
}
bool CommonInputs(id<MTLCommandBuffer> commands, const Inputs& in, const Frame& f,
                  id<MTLDevice> device, const Configuration& c, bool interpolation,
                  std::string& error) {
  if (!commands || commands.commandQueue.device != device ||
      commands.status != MTLCommandBufferStatusNotEnqueued || !commands.retainedReferences) {
    error = "temporal work requires an uncommitted retained command buffer on its device";
    return false;
  }
  const uint32_t cw = interpolation ? c.output_width : c.input_width,
                 ch = interpolation ? c.output_height : c.input_height;
  if (!TextureMatches(in.color, device, MTLPixelFormatRGBA16Float, cw, ch,
                      MTLTextureUsageShaderRead) ||
      !TextureMatches(in.depth, device, MTLPixelFormatR32Float, c.input_width, c.input_height,
                      MTLTextureUsageShaderRead) ||
      !TextureMatches(in.motion, device, MTLPixelFormatRG16Float, c.input_width, c.input_height,
                      MTLTextureUsageShaderRead) ||
      (!interpolation && !TextureMatches(in.reactive, device, MTLPixelFormatR8Unorm, c.input_width,
                                         c.input_height, MTLTextureUsageShaderRead)) ||
      (!interpolation && c.method == Method::kTaa &&
       !TextureMatches(in.previous_depth, device, MTLPixelFormatR32Float,
                       c.input_width, c.input_height, MTLTextureUsageShaderRead)) ||
      (!interpolation && c.method == Method::kMetalFx && in.exposure &&
       !TextureMatches(in.exposure, device, MTLPixelFormatR16Float, 1, 1,
                       MTLTextureUsageShaderRead))) {
    error = "temporal input device, extent, format, usage or tracking mismatch";
    return false;
  }
  if (in.color == in.depth || in.color == in.motion || in.depth == in.motion) {
    error = "temporal input aspects alias";
    return false;
  }
  if (!std::isfinite(f.aspect) || f.aspect <= 0) {
    error = "temporal projection aspect must be finite and positive";
    return false;
  }
  return true;
}
struct alignas(16) TaaConstants {
  std::array<uint32_t, 2> extent{};
  std::array<float, 2> jitter{};
  float near_plane = 0.1f, far_plane = 1000.0f, history_weight = 0.9f, previous_pre_exposure = 1;
  float current_pre_exposure = 1;
  uint32_t reset = 1, reversed = 0, padding = 0;
  float previous_near_plane = 0.1f, previous_far_plane = 1000.0f;
  std::array<float, 2> padding2{};
};
static_assert(sizeof(TaaConstants) == 4 * 16);
bool Copy(id<MTLBlitCommandEncoder> encoder, id<MTLTexture> source, id<MTLTexture> destination) {
  if (!encoder || !source || !destination || source == destination ||
      source.width != destination.width || source.height != destination.height ||
      source.pixelFormat != destination.pixelFormat)
    return false;
  [encoder copyFromTexture:source
               sourceSlice:0
               sourceLevel:0
              sourceOrigin:MTLOriginMake(0, 0, 0)
                sourceSize:MTLSizeMake(source.width, source.height, 1)
                 toTexture:destination
          destinationSlice:0
          destinationLevel:0
         destinationOrigin:MTLOriginMake(0, 0, 0)];
  return true;
}
}  // namespace
void KeepUntilCompleted(id<MTLCommandBuffer> commands, OutputLease lease) {
  if (!commands || !lease) return;
  // Release at completion, not when a retained command-buffer/capture object
  // eventually destroys its handler. Keeping a completed command alive must
  // not pin a bounded temporal output slot indefinitely.
  auto held = std::make_shared<OutputLease>(std::move(lease));
  [commands addCompletedHandler:^(id<MTLCommandBuffer>) {
    held->reset();
  }];
}
Capabilities QueryCapabilities(id<MTLDevice> device) {
  if (!device) return {};
  return {[MTLFXTemporalScalerDescriptor supportsDevice:device] != NO,
          [MTLFXFrameInterpolatorDescriptor supportsDevice:device] != NO,
          [MTLFXTemporalScalerDescriptor supportedInputContentMinScaleForDevice:device],
          [MTLFXTemporalScalerDescriptor supportedInputContentMaxScaleForDevice:device]};
}
struct AntiAliasing::State {
  struct Slot {
    id<MTLTexture> color = nil, depth = nil, exposure = nil;
    __weak id<MTLCommandBuffer> owner = nil;
    std::weak_ptr<void> lease;
  };
  id<MTLDevice> device = nil;
  id<MTLCommandQueue> queue = nil;
  Configuration config{};
  History history;
  id<MTLFXTemporalScaler> scaler = nil;
  id<MTLComputePipelineState> taa = nil;
  std::array<Slot, 3> slots{};
  size_t cursor = 0, previous = 0, owned = 0, limit = 0;
  std::shared_ptr<FailureState> failures = std::make_shared<FailureState>();
  uint64_t seen_failure = 0;
};
AntiAliasing::AntiAliasing() = default;
AntiAliasing::~AntiAliasing() = default;
size_t OwnedTextureBudget(id<MTLDevice> device) {
  return device ? std::min<size_t>(kMaximumOwnedBytes, device.recommendedMaxWorkingSetSize / 4)
                : 0;
}
void AntiAliasing::Reset() {
  if (state_) state_->history.Reset();
}
size_t AntiAliasing::owned_bytes() const { return state_ ? state_->owned : 0; }
bool AntiAliasing::Configure(id<MTLDevice> device, id<MTLLibrary> library, Configuration c,
                             size_t budget, std::string& error) {
  @autoreleasepool {
    error.clear();
    budget = std::min(budget, kMaximumOwnedBytes);
    if (!device || !ValidConfiguration(c) || !budget) {
      error = "invalid temporal AA configuration";
      return false;
    }
    if (state_ && state_->device == device && state_->config == c && state_->limit == budget)
      return true;
    if (!rejected_error_.empty() && rejected_device_ == device && rejected_library_ == library &&
        rejected_config_ == c && rejected_budget_ == budget) {
      error = rejected_error_;
      return false;
    }
    // History reset does not make an unsupported configuration or an
    // insufficient history budget possible. Transient driver/allocation
    // failures remain retryable after memory pressure subsides.
    bool permanent_rejection = false;
    const bool configured = [&]() {
    auto next = std::make_unique<State>();
    next->device = device;
    next->config = c;
    next->limit = budget;
    MTLTextureUsage output_usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    if (c.method == Method::kMetalFx) {
      const auto caps = QueryCapabilities(device);
      const float sx = float(c.output_width) / c.input_width,
                  sy = float(c.output_height) / c.input_height;
      if (!caps.temporal || sx < caps.minimum_scale || sy < caps.minimum_scale ||
          sx > caps.maximum_scale || sy > caps.maximum_scale) {
        permanent_rejection = true;
        error = "MetalFX temporal scale is unsupported by this device";
        return false;
      }
      auto descriptor = [MTLFXTemporalScalerDescriptor new];
      descriptor.inputWidth = c.input_width;
      descriptor.inputHeight = c.input_height;
      descriptor.outputWidth = c.output_width;
      descriptor.outputHeight = c.output_height;
      descriptor.colorTextureFormat = MTLPixelFormatRGBA16Float;
      descriptor.depthTextureFormat = MTLPixelFormatR32Float;
      descriptor.motionTextureFormat = MTLPixelFormatRG16Float;
      descriptor.outputTextureFormat = MTLPixelFormatRGBA16Float;
      descriptor.reactiveMaskTextureEnabled = YES;
      descriptor.reactiveMaskTextureFormat = MTLPixelFormatR8Unorm;
      descriptor.autoExposureEnabled = NO;
      descriptor.requiresSynchronousInitialization = YES;
      next->scaler = [descriptor newTemporalScalerWithDevice:device];
      if (!next->scaler) {
        error = "MetalFX temporal scaler creation failed";
        return false;
      }
      output_usage |= next->scaler.outputTextureUsage;
    } else {
      if (!library || library.device != device) {
        permanent_rejection = true;
        error = "native TAA requires its compiled shader library";
        return false;
      }
      NSError* native_error = nil;
      auto function = [library newFunctionWithName:@"liberty_temporal_aa"];
      if (!function) {
        permanent_rejection = true;
        error = "native TAA entry point is missing";
        return false;
      }
      next->taa = [device newComputePipelineStateWithFunction:function error:&native_error];
      if (!next->taa) {
        error = native_error.localizedDescription.UTF8String;
        return false;
      }
    }
    size_t required = 0;
    for (size_t i = 0; i < next->slots.size(); ++i) {
      const auto add = [&](MTLPixelFormat format, NSUInteger width, NSUInteger height,
                           MTLTextureUsage usage) {
        const size_t bytes = AllocationSize(device, format, width, height, usage);
        if (!bytes || required > budget || bytes > budget - required) return false;
        required += bytes;
        return true;
      };
      if (!add(MTLPixelFormatRGBA16Float, c.output_width, c.output_height, output_usage) ||
          (c.method == Method::kTaa &&
           !add(MTLPixelFormatR32Float, c.output_width, c.output_height,
                MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite)) ||
          (c.method == Method::kMetalFx &&
           !add(MTLPixelFormatR16Float, 1, 1, MTLTextureUsageShaderRead))) {
        error = "temporal owned-texture budget cannot hold complete history";
        permanent_rejection = true;
        return false;
      }
    }
    for (auto& slot : next->slots) {
      slot.color =
          Allocate(device, MTLPixelFormatRGBA16Float, c.output_width, c.output_height, output_usage,
                   @"Liberty temporal AA history", budget, next->owned, error);
      if (!slot.color) return false;
      if (c.method == Method::kMetalFx) {
        slot.exposure = Allocate(device, MTLPixelFormatR16Float, 1, 1, MTLTextureUsageShaderRead,
                                 @"Liberty tone-map exposure hint", budget, next->owned, error);
        if (!slot.exposure) return false;
      }
      if (c.method == Method::kTaa) {
        slot.depth = Allocate(device, MTLPixelFormatR32Float, c.output_width, c.output_height,
                              MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite,
                              @"Liberty TAA history depth", budget, next->owned, error);
        if (!slot.depth) return false;
      }
    }
    state_ = std::move(next);
    return true;
    }();
    if (configured) {
      rejected_error_.clear();
      rejected_device_ = nil;
      rejected_library_ = nil;
    } else if (permanent_rejection) {
      rejected_device_ = device;
      rejected_library_ = library;
      rejected_config_ = c;
      rejected_budget_ = budget;
      rejected_error_ = error;
    }
    return configured;
  }
}
bool AntiAliasing::Encode(id<MTLCommandBuffer> commands, const Inputs& in, const Frame& frame,
                          Output& out, std::string& error) {
  @autoreleasepool {
    error.clear();
    out = {};
    if (!state_) {
      error = "temporal AA is not configured";
      return false;
    }
    auto& s = *state_;
    if (frame.color_domain != ColorDomain::kSceneLinear) {
      error = "temporal AA must precede display tone mapping";
      return false;
    }
    if (!CommonInputs(commands, in, frame, s.device, s.config, false, error)) return false;
    if (s.queue && s.queue != commands.commandQueue) {
      error = "temporal AA cannot cross command queues";
      return false;
    }
    const auto failure = s.failures->count.load(std::memory_order_acquire);
    if (failure != s.seen_failure) {
      s.history.Reset();
      s.seen_failure = failure;
    }
    const auto decision = s.history.Inspect(frame, true);
    if (!decision.accepted) {
      error = decision.reason;
      return false;
    }
    auto& slot = s.slots[s.cursor];
    if (!Available(slot.owner) || !slot.lease.expired()) {
      error = "temporal AA output is still in flight or leased";
      return false;
    }
    if (slot.color == in.color) {
      error = "temporal AA input aliases the next history output";
      return false;
    }
    if (s.scaler) {
      auto scaler = s.scaler;
      if ((in.color.usage & scaler.colorTextureUsage) != scaler.colorTextureUsage ||
          (in.depth.usage & scaler.depthTextureUsage) != scaler.depthTextureUsage ||
          (in.motion.usage & scaler.motionTextureUsage) != scaler.motionTextureUsage ||
          (in.reactive.usage & scaler.reactiveTextureUsage) != scaler.reactiveTextureUsage) {
        error = "MetalFX temporal input usage contract is unmet";
        return false;
      }
      if (in.exposure) {
        scaler.exposureTexture = in.exposure;
      } else {
      // MetalFX requires a 1x1 R16Float exposure texture. Store a half-float
      // value, not the low bytes of the float used by the title's tone mapper.
      const _Float16 exposure_value = _Float16(frame.exposure);
      if (!std::isfinite(float(exposure_value)) || exposure_value <= 0) {
        error = "MetalFX exposure is not representable as a positive finite half-float";
        return false;
      }
      // The CPU staging allocation is immutable and retained by this command,
      // never rewritten in flight.
      std::array<uint8_t, 256> exposure_bytes{};
      std::memcpy(exposure_bytes.data(), &exposure_value, sizeof(exposure_value));
      auto exposure = [s.device newBufferWithBytes:exposure_bytes.data()
                                            length:exposure_bytes.size()
                                           options:MTLResourceStorageModeShared];
      auto transfer = [commands blitCommandEncoder];
      if (!exposure || !transfer) {
        error = "temporal exposure upload allocation failed";
        return false;
      }
      [transfer copyFromBuffer:exposure
                  sourceOffset:0
             sourceBytesPerRow:256
           sourceBytesPerImage:256
                    sourceSize:MTLSizeMake(1, 1, 1)
                     toTexture:slot.exposure
              destinationSlice:0
              destinationLevel:0
             destinationOrigin:MTLOriginMake(0, 0, 0)];
      [transfer endEncoding];
      scaler.exposureTexture = slot.exposure;
      }
      scaler.inputContentWidth = s.config.input_width;
      scaler.inputContentHeight = s.config.input_height;
      scaler.colorTexture = in.color;
      scaler.depthTexture = in.depth;
      scaler.motionTexture = in.motion;
      scaler.reactiveMaskTexture = in.reactive;
      scaler.outputTexture = slot.color;
      scaler.jitterOffsetX = frame.jitter[0];
      scaler.jitterOffsetY = frame.jitter[1];
      scaler.motionVectorScaleX = 1;
      scaler.motionVectorScaleY = 1;
      scaler.preExposure = frame.pre_exposure;
      scaler.depthReversed = s.config.depth_reversed;
      scaler.reset = decision.reset;
      [scaler encodeToCommandBuffer:commands];
    } else {
      TaaConstants c;
      c.extent = {s.config.input_width, s.config.input_height};
      c.jitter = frame.jitter;
      c.near_plane = frame.near_plane;
      c.far_plane = frame.far_plane;
      c.reset = decision.reset;
      c.reversed = s.config.depth_reversed;
      c.current_pre_exposure = frame.pre_exposure;
      c.previous_pre_exposure =
          decision.reset ? frame.pre_exposure : s.history.previous().pre_exposure;
      const auto& previous_frame = decision.reset ? frame : s.history.previous();
      c.previous_near_plane = previous_frame.near_plane;
      c.previous_far_plane = previous_frame.far_plane;
      auto encoder = [commands computeCommandEncoder];
      if (!encoder) {
        error = "native TAA encoder creation failed";
        return false;
      }
      encoder.label = @"Liberty TAA reprojection and disocclusion";
      [encoder setComputePipelineState:s.taa];
      const auto& prior = s.slots[s.previous];
      std::array<id<MTLTexture>, 9> textures{in.color,
                                             in.depth,
                                             in.motion,
                                             in.reactive,
                                             decision.reset ? in.color : prior.color,
                                             decision.reset ? in.depth : prior.depth,
                                             slot.color,
                                             slot.depth,
                                             in.previous_depth};
      [encoder setTextures:textures.data() withRange:NSMakeRange(0, textures.size())];
      [encoder setBytes:&c length:sizeof(c) atIndex:0];
      const NSUInteger width = s.taa.threadExecutionWidth,
                       height = std::max<NSUInteger>(
                           1, std::min<NSUInteger>(8, s.taa.maxTotalThreadsPerThreadgroup / width));
      [encoder dispatchThreads:MTLSizeMake(s.config.input_width, s.config.input_height, 1)
          threadsPerThreadgroup:MTLSizeMake(width, height, 1)];
      [encoder endEncoding];
    }
    s.queue = commands.commandQueue;
    slot.owner = commands;
    s.previous = s.cursor;
    s.cursor = (s.cursor + 1) % s.slots.size();
    s.history.Commit(frame);
    ObserveFailure(commands, s.failures);
    auto lease = std::make_shared<LeasedImages>();
    lease->first = slot.color;
    lease->second = slot.depth;
    slot.lease = lease;
    out = {slot.color, decision.reset, std::move(lease)};
    return true;
  }
}
struct FrameInterpolation::State {
  struct Slot {
    id<MTLTexture> scene = nil, real = nil, generated = nil;
    __weak id<MTLCommandBuffer> owner = nil;
    std::weak_ptr<void> lease;
  };
  id<MTLDevice> device = nil;
  id<MTLCommandQueue> queue = nil;
  Configuration config{};
  History history;
  id<MTLFXFrameInterpolator> interpolator = nil;
  id<MTLComputePipelineState> composite = nil, overlay = nil;
  std::array<Slot, 2> slots{};
  size_t cursor = 0, previous = 0, owned = 0, limit = 0;
  bool encoded = false, restart_instance = false;
  std::shared_ptr<FailureState> failures = std::make_shared<FailureState>();
  uint64_t seen_failure = 0;
  id<MTLFXFrameInterpolator> CreateInterpolator() const {
    auto descriptor = [MTLFXFrameInterpolatorDescriptor new];
    descriptor.inputWidth = config.input_width;
    descriptor.inputHeight = config.input_height;
    descriptor.outputWidth = config.output_width;
    descriptor.outputHeight = config.output_height;
    descriptor.colorTextureFormat = descriptor.outputTextureFormat = descriptor.uiTextureFormat =
        MTLPixelFormatRGBA16Float;
    descriptor.depthTextureFormat = MTLPixelFormatR32Float;
    descriptor.motionTextureFormat = MTLPixelFormatRG16Float;
    return [descriptor newFrameInterpolatorWithDevice:device];
  }
};
FrameInterpolation::FrameInterpolation() = default;
FrameInterpolation::~FrameInterpolation() = default;
void FrameInterpolation::Reset() {
  if (state_) {
    state_->history.Reset();
    state_->restart_instance = state_->encoded;
  }
}
size_t FrameInterpolation::owned_bytes() const { return state_ ? state_->owned : 0; }
bool FrameInterpolation::Configure(id<MTLDevice> device, id<MTLLibrary> library, Configuration c,
                                   size_t budget, std::string& error) {
  @autoreleasepool {
    error.clear();
    budget = std::min(budget, kMaximumOwnedBytes);
    if (!device || !ValidConfiguration(c) || !budget || !QueryCapabilities(device).interpolation) {
      error = "unsupported MetalFX frame interpolation configuration";
      return false;
    }
    if (state_ && state_->device == device && state_->config == c && state_->limit == budget)
      return true;
    auto next = std::make_unique<State>();
    next->device = device;
    next->config = c;
    next->limit = budget;
    if (!library || library.device != device) {
      error = "UI composition shader library is unavailable";
      return false;
    }
    NSError* native_error = nil;
    auto ui_function = [library newFunctionWithName:@"liberty_temporal_ui_over"];
    if (!ui_function) {
      error = "temporal UI composition shader is unavailable";
      return false;
    }
    next->composite = [device newComputePipelineStateWithFunction:ui_function error:&native_error];
    if (!next->composite) {
      error = native_error.localizedDescription.UTF8String;
      return false;
    }
    auto overlay_function = [library newFunctionWithName:@"liberty_temporal_ui_over_inplace"];
    if (!overlay_function) {
      error = "temporal overlay shader is unavailable";
      return false;
    }
    next->overlay = [device newComputePipelineStateWithFunction:overlay_function
                                                          error:&native_error];
    if (!next->overlay) {
      error = native_error.localizedDescription.UTF8String;
      return false;
    }
    next->interpolator = next->CreateInterpolator();
    if (!next->interpolator) {
      error = "MetalFX frame interpolator creation failed";
      return false;
    }
    for (auto& slot : next->slots) {
      slot.scene = Allocate(device, MTLPixelFormatRGBA16Float, c.output_width, c.output_height,
                            MTLTextureUsageShaderRead | next->interpolator.colorTextureUsage,
                            @"Liberty previous real scene", budget, next->owned, error);
      if (!slot.scene) return false;
      slot.real = Allocate(device, MTLPixelFormatRGBA16Float, c.output_width, c.output_height,
                           MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite,
                           @"Liberty real presentation image", budget, next->owned, error);
      if (!slot.real) return false;
      slot.generated = Allocate(device, MTLPixelFormatRGBA16Float, c.output_width, c.output_height,
                                MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite |
                                    next->interpolator.outputTextureUsage,
                                @"Liberty MetalFX generated image", budget, next->owned, error);
      if (!slot.generated) return false;
    }
    state_ = std::move(next);
    return true;
  }
}
bool FrameInterpolation::Encode(id<MTLCommandBuffer> commands, const Inputs& in, UiInput ui_input,
                                const Frame& frame, InterpolationOutput& out, std::string& error) {
  @autoreleasepool {
    error.clear();
    out = {};
    if (!state_) {
      error = "frame interpolation is not configured";
      return false;
    }
    auto& s = *state_;
    if (frame.color_domain != ColorDomain::kDisplayLinear &&
        frame.color_domain != ColorDomain::kDisplayPerceptual) {
      error = "frame interpolation must follow tone mapping";
      return false;
    }
    if ((ui_input.mode == UiMode::kNone) != (ui_input.image == nil)) {
      error = "UI composition mode disagrees with its image";
      return false;
    }
    auto ui = ui_input.image;
    if (!CommonInputs(commands, in, frame, s.device, s.config, true, error)) return false;
    if (s.queue && s.queue != commands.commandQueue) {
      error = "frame interpolation cannot cross command queues";
      return false;
    }
    auto fx = s.interpolator;
    if ((in.depth.usage & fx.depthTextureUsage) != fx.depthTextureUsage ||
        (in.motion.usage & fx.motionTextureUsage) != fx.motionTextureUsage ||
        (ui && !TextureMatches(ui, s.device, MTLPixelFormatRGBA16Float, s.config.output_width,
                               s.config.output_height, fx.uiTextureUsage))) {
      error = "frame interpolation input usage or UI contract is unmet";
      return false;
    }
    const auto failures = s.failures->count.load(std::memory_order_acquire);
    if (failures != s.seen_failure) {
      s.history.Reset();
      s.seen_failure = failures;
    }
    const auto decision = s.history.Inspect(frame);
    if (!decision.accepted) {
      error = decision.reason;
      return false;
    }
    auto& slot = s.slots[s.cursor];
    if (!Available(slot.owner) || !slot.lease.expired()) {
      error = "interpolated pair is still in flight or leased";
      return false;
    }
    if (s.restart_instance || (decision.reset && s.encoded)) {
      // The reset-and-reuse GPU regression on the tested MetalFX runtime kept
      // stale scene state after a cut. Recreate only the effect at discontinuities;
      // the bounded texture ring and ordinary continuous-frame path are reused.
      auto replacement = s.CreateInterpolator();
      if (!replacement) {
        error = "MetalFX history discontinuity reset failed";
        return false;
      }
      s.interpolator = replacement;
      fx = replacement;
      s.restart_instance = false;
    }
    if (in.color == slot.scene || in.color == slot.real || in.color == slot.generated ||
        ui == slot.scene || ui == slot.real || ui == slot.generated) {
      error = "interpolation source aliases an output slot";
      return false;
    }
    auto copy = [commands blitCommandEncoder];
    if (!copy) {
      error = "frame interpolation snapshot encoder failed";
      return false;
    }
    copy.label = @"Liberty immutable frame-generation inputs";
    const bool overlay = ui_input.mode == UiMode::kPremultipliedOverlay;
    const bool copied =
        Copy(copy, in.color, slot.scene) && (overlay || Copy(copy, ui ? ui : in.color, slot.real));
    [copy endEncoding];
    if (!copied) {
      error = "frame interpolation snapshot mismatch";
      return false;
    }
    if (overlay) {
      auto encoder = [commands computeCommandEncoder];
      if (!encoder) {
        error = "temporal UI composition encoder failed";
        return false;
      }
      encoder.label = @"Liberty current UI composition";
      [encoder setComputePipelineState:s.composite];
      [encoder setTexture:slot.scene atIndex:0];
      [encoder setTexture:ui atIndex:1];
      [encoder setTexture:slot.real atIndex:2];
      const NSUInteger w = s.composite.threadExecutionWidth,
                       h = std::max<NSUInteger>(
                           1,
                           std::min<NSUInteger>(8, s.composite.maxTotalThreadsPerThreadgroup / w));
      [encoder dispatchThreads:MTLSizeMake(s.config.output_width, s.config.output_height, 1)
          threadsPerThreadgroup:MTLSizeMake(w, h, 1)];
      [encoder endEncoding];
    }
    fx.colorTexture = slot.scene;
    fx.prevColorTexture = decision.reset ? slot.scene : s.slots[s.previous].scene;
    fx.depthTexture = in.depth;
    fx.motionTexture = in.motion;
    fx.outputTexture = slot.generated;
    // Keep a premultiplied layer outside interpolation entirely: it is blended
    // after the midpoint has been generated, and never enters flow estimation.
    fx.uiTexture = overlay ? nil : ui;
    fx.uiTextureComposited = ui_input.mode == UiMode::kComposited;
    fx.motionVectorScaleX = float(s.config.output_width) / s.config.input_width;
    fx.motionVectorScaleY = float(s.config.output_height) / s.config.input_height;
    fx.jitterOffsetX = frame.jitter[0];
    fx.jitterOffsetY = frame.jitter[1];
    fx.deltaTime = decision.delta_seconds > 0 ? decision.delta_seconds : 1.0f / 60.0f;
    fx.nearPlane = frame.near_plane;
    fx.farPlane = frame.far_plane;
    fx.fieldOfView = frame.field_of_view;
    fx.aspectRatio = frame.aspect;
    fx.depthReversed = s.config.depth_reversed;
    fx.shouldResetHistory = decision.reset;
    [fx encodeToCommandBuffer:commands];
    if (overlay && !decision.reset) {
      auto encoder = [commands computeCommandEncoder];
      if (!encoder) {
        s.history.Reset();
        error = "generated-frame UI encoder failed";
        return false;
      }
      encoder.label = @"Liberty UI after frame interpolation";
      [encoder setComputePipelineState:s.overlay];
      [encoder setTexture:slot.generated atIndex:0];
      [encoder setTexture:ui atIndex:1];
      const NSUInteger w = s.overlay.threadExecutionWidth,
                       h = std::max<NSUInteger>(
                           1, std::min<NSUInteger>(8, s.overlay.maxTotalThreadsPerThreadgroup / w));
      [encoder dispatchThreads:MTLSizeMake(s.config.output_width, s.config.output_height, 1)
          threadsPerThreadgroup:MTLSizeMake(w, h, 1)];
      [encoder endEncoding];
    }
    s.queue = commands.commandQueue;
    slot.owner = commands;
    s.previous = s.cursor;
    s.cursor = (s.cursor + 1) % s.slots.size();
    s.history.Commit(frame);
    ObserveFailure(commands, s.failures);
    s.encoded = true;
    // Keep the effect object alive until its encoded private history work is done.
    [commands addCompletedHandler:^(id<MTLCommandBuffer>) {
      (void)fx;
    }];
    auto lease = std::make_shared<LeasedImages>();
    lease->first = slot.generated;
    lease->second = slot.real;
    slot.lease = lease;
    out = {decision.reset ? nil : slot.generated, slot.real, decision.reset, std::move(lease)};
    return true;
  }
}
}  // namespace rex::graphics::gta4_metal::temporal
