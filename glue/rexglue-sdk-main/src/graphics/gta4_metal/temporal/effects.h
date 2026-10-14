#pragma once
#import <Metal/Metal.h>

#include <cmath>
#include <memory>
#include <string>

#include "history.h"

namespace rex::graphics::gta4_metal::temporal {
struct Inputs {
  // Linear, scene-only RGBA16Float; device depth R32Float; unjittered
  // current-to-previous RG16Float vectors in INPUT pixels; R8Unorm reactive.
  id<MTLTexture> color = nil, depth = nil, motion = nil, reactive = nil;
  // Native TAA only: expected previous device depth of the current surface,
  // R32Float at input resolution and registered to the jittered scene. -1 is
  // invalid, including unsupported nonstandard depth viewports. MetalFX ignores it.
  id<MTLTexture> previous_depth = nil;
  // Optional GPU-produced 1x1 R16Float MetalFX exposure. Scalar Frame::exposure
  // remains the fallback for callers without a title exposure texture.
  id<MTLTexture> exposure = nil;
};
// Keep the lease through the LAST consumer submission. Holding it prevents
// output-slot recycling; KeepUntilCompleted transfers this duty to a command.
using OutputLease = std::shared_ptr<void>;
void KeepUntilCompleted(id<MTLCommandBuffer>, OutputLease);
struct Output {
  id<MTLTexture> color = nil;
  bool history_reset = true;
  OutputLease lease;
};
struct InterpolationOutput {
  id<MTLTexture> generated = nil, real = nil;
  bool history_reset = true;
  OutputLease lease;
};
enum class UiMode { kNone, kPremultipliedOverlay, kComposited };
struct UiInput {
  UiMode mode = UiMode::kNone;
  id<MTLTexture> image = nil;
};
struct Capabilities {
  bool temporal = false, interpolation = false;
  float minimum_scale = 1, maximum_scale = 1;
};
Capabilities QueryCapabilities(id<MTLDevice> device);
// Includes device alignment, not just texel payload. Keeps each effect within
// a bounded share of the device's recommended working set while admitting 4K.
size_t OwnedTextureBudget(id<MTLDevice> device);
id<MTLLibrary> CreateLibrary(id<MTLDevice> device, std::string& error);
// Serialized by the title renderer. Uses its existing queue: no commit,
// waitUntilCompleted, global context, or second presentation scheduler here.
// Submit each accepted command buffer, or call Reset to abandon its history.
class AntiAliasing {
 public:
  AntiAliasing();
  ~AntiAliasing();
  AntiAliasing(const AntiAliasing&) = delete;
  AntiAliasing& operator=(const AntiAliasing&) = delete;
  bool Configure(id<MTLDevice>, id<MTLLibrary>, Configuration, size_t owned_texture_budget,
                 std::string&);
  bool Encode(id<MTLCommandBuffer>, const Inputs&, const Frame&, Output&, std::string&);
  void Reset();
  size_t owned_bytes() const;

 private:
  struct State;
  std::unique_ptr<State> state_;
  id<MTLDevice> rejected_device_ = nil;
  id<MTLLibrary> rejected_library_ = nil;
  Configuration rejected_config_{};
  size_t rejected_budget_ = 0;
  std::string rejected_error_;
};
class FrameInterpolation {
 public:
  FrameInterpolation();
  ~FrameInterpolation();
  FrameInterpolation(const FrameInterpolation&) = delete;
  FrameInterpolation& operator=(const FrameInterpolation&) = delete;
  bool Configure(id<MTLDevice>, id<MTLLibrary>, Configuration, size_t owned_texture_budget,
                 std::string&);
  // Scene color is at output resolution; depth/motion retain input resolution.
  // UI is explicit: prefer a premultiplied overlay or draw it afterward.
  bool Encode(id<MTLCommandBuffer>, const Inputs&, UiInput, const Frame&, InterpolationOutput&,
              std::string&);
  void Reset();
  size_t owned_bytes() const;

 private:
  struct State;
  std::unique_ptr<State> state_;
};
}  // namespace rex::graphics::gta4_metal::temporal
