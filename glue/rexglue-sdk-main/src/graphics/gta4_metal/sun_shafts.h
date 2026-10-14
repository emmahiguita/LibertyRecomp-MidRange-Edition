#pragma once
#import <Metal/Metal.h>
#include "../gta4_native/sun_shafts_parameters.h"
#include <array>
#include <memory>
#include <string>
namespace rex::ui::metal {
struct MetalContext;
}
namespace rex::graphics::gta4_metal {
// Uses sun_shafts_ps.glsl, compiled to MSL by compile_passes.py.
class SunShafts {
 public:
  id<MTLTexture> Record(const std::shared_ptr<ui::metal::MetalContext>& context,
                        id<MTLCommandBuffer> commands, id<MTLTexture> scene,
                        id<MTLTexture> depth, id<MTLBuffer> cloud_mask,
                        const gta4_native::SunShaftParameters& parameters, std::string& error);
  void ReleaseExtentResources() { images_ = {}; }

 private:
  id<MTLRenderPipelineState> pipeline_ = nil;
  id<MTLSamplerState> sampler_ = nil;
  MTLPixelFormat format_ = MTLPixelFormatInvalid;
  std::array<id<MTLTexture>, 4> images_{};
};
}  // namespace rex::graphics::gta4_metal
