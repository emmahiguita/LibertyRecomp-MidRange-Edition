#pragma once
#import <Metal/Metal.h>
#include <rex/graphics/gta4_native/anti_aliasing_policy.h>
#include "../../ui/metal/output_transfer.h"
#include <array>
#include <memory>
#include <string>
#include <string_view>

namespace rex::ui::metal { struct MetalContext; }
namespace rex::graphics::gta4_metal {

// One ordered pass chain on the renderer's existing queue. Lookup textures and
// pipelines are persistent; extent-dependent textures are reused, not per-frame.
class PostProcessing {
 public:
  bool Initialize(std::shared_ptr<ui::metal::MetalContext> context, std::string& error);
  bool Record(id<MTLCommandBuffer> commands, id<MTLTexture> source, id<MTLTexture> destination,
              gta4_native::AntiAliasingMode mode, std::string_view quality, bool dither,
              std::string& error);
  void ReleaseExtentResources();
  size_t allocated_bytes() const;
 private:
  bool EnsureSmaa(size_t quality,std::string& error);
  bool EnsureExtent(NSUInteger width, NSUInteger height, std::string& error);
  std::shared_ptr<ui::metal::MetalContext> context_;
  ui::metal::OutputTransfer transfer_;
  std::array<std::array<id<MTLRenderPipelineState>, 3>, 4> pipelines_{};
  id<MTLSamplerState> point_ = nil, linear_ = nil;
  id<MTLTexture> area_ = nil, search_ = nil;
  std::array<id<MTLTexture>, 3> images_{};
};
}  // namespace rex::graphics::gta4_metal
