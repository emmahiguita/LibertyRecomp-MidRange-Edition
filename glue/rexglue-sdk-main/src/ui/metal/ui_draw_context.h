#pragma once

#import <Metal/Metal.h>

#include <rex/ui/presenter.h>

#include "upload_arena.h"

namespace rex::ui::metal {

// Borrowed encoder and upload arena. Neither survives the surrounding frame.
class MetalUIDrawContext final : public UIDrawContext {
 public:
  MetalUIDrawContext(uint32_t width, uint32_t height,
                    id<MTLRenderCommandEncoder> encoder, UploadArena& uploads)
      : UIDrawContext(width, height), encoder(encoder), uploads(uploads) {}
  // Presenter-backed contexts use the corresponding base constructor.
  MetalUIDrawContext(Presenter& presenter, uint32_t width, uint32_t height,
                    id<MTLRenderCommandEncoder> encoder, UploadArena& uploads)
      : UIDrawContext(presenter, width, height), encoder(encoder), uploads(uploads) {}

  id<MTLRenderCommandEncoder> encoder;
  UploadArena& uploads;
  MTLPixelFormat target_format = MTLPixelFormatBGRA8Unorm;
  bool linear_output = false;

};

}  // namespace rex::ui::metal
