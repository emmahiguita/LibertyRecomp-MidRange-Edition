#pragma once
#import <Metal/Metal.h>
#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <string>

namespace rex::ui::metal {
struct MetalContext;
enum class PresentationEffect : size_t {
  kBilinear, kBilinearDither, kCasSharpen, kCasSharpenDither,
  kCasResample, kCasResampleDither, kFsrEasu, kFsrRcas, kFsrRcasDither, kCount
};

// Uses the exact existing presentation shaders and constant layouts. The caller
// owns passes, target lifetimes and effect ordering; this owns reusable pipelines.
class PresentationEffects {
 public:
  bool Initialize(std::shared_ptr<MetalContext> context, std::string& error);
  bool Draw(id<MTLRenderCommandEncoder> encoder, PresentationEffect effect,
            id<MTLTexture> source, id<MTLTexture> target,
            MTLViewport viewport, std::span<const std::byte> constants,
            std::string& error);
 private:
  std::shared_ptr<MetalContext> context_;
  static constexpr size_t kEffectCount = size_t(PresentationEffect::kCount);
  std::array<std::array<id<MTLRenderPipelineState>, 2>, kEffectCount> pipelines_{};
  id<MTLSamplerState> sampler_ = nil;
};
}  // namespace rex::ui::metal
