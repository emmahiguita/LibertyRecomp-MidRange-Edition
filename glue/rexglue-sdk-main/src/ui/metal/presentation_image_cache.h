#pragma once

#import <Metal/Metal.h>
#include <array>
#include <memory>
#include <string>

#include "presentation_cache_policy.h"

namespace rex::ui::metal {
struct MetalContext;

// Main-thread-owned, private tracked image on MetalContext's existing queue.
// Metal orders writes after previous reads; retained command buffers keep an
// old image alive when resize/reconnect replaces the cache's reference.
class PresentationImageCache {
 public:
  bool Initialize(std::shared_ptr<MetalContext> context, std::string& error);
  id<MTLTexture> Prepare(uint32_t width, uint32_t height, MTLPixelFormat format,
                         std::string& error);
  bool Draw(id<MTLRenderCommandEncoder> encoder, id<MTLTexture> destination,
            std::string& error) const;
  void Reset() { policy.Reset(); image_ = nil; }
  id<MTLTexture> image() const { return image_; }

  PresentationCachePolicy policy;

 private:
  std::shared_ptr<MetalContext> context_;
  std::array<id<MTLRenderPipelineState>, 2> pipelines_{};
  id<MTLTexture> image_ = nil;
};

}  // namespace rex::ui::metal
