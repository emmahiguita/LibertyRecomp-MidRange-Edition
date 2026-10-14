#include "presentation_image_cache.h"

#include "context.h"

namespace rex::ui::metal {
namespace {
constexpr MTLPixelFormat kFormats[] = {MTLPixelFormatBGRA8Unorm, MTLPixelFormatRGBA16Float};
}

bool PresentationImageCache::Initialize(std::shared_ptr<MetalContext> context,
                                       std::string& error) {
  error.clear();
  if (context_) return true;
  if (!context || !context->device || !context->queue || !context->ui_library) {
    error = "Presentation cache requires the existing Metal context"; return false;
  }
  auto vertex = [context->ui_library newFunctionWithName:@"liberty_present_vertex"];
  auto fragment = [context->ui_library newFunctionWithName:@"liberty_cached_present_fragment"];
  if (!vertex || !fragment) { error = "Missing presentation cache copy shader"; return false; }
  std::array<id<MTLRenderPipelineState>, 2> candidates{};
  for (size_t i = 0; i < candidates.size(); ++i) {
    auto descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.label = @"Liberty cached game image copy";
    descriptor.vertexFunction = vertex;
    descriptor.fragmentFunction = fragment;
    descriptor.colorAttachments[0].pixelFormat = kFormats[i];
    NSError* native_error = nil;
    candidates[i] = [context->device newRenderPipelineStateWithDescriptor:descriptor error:&native_error];
    if (!candidates[i]) {
      error = MetalError(native_error, "Presentation cache copy pipeline failed"); return false;
    }
  }
  pipelines_ = candidates;
  context_ = std::move(context);
  return true;
}

id<MTLTexture> PresentationImageCache::Prepare(uint32_t width, uint32_t height,
                                              MTLPixelFormat format, std::string& error) {
  error.clear();
  if (!context_ || !width || !height || width > 16384 || height > 16384 ||
      (format != kFormats[0] && format != kFormats[1]) ||
      uint64_t(width) * height * (format == MTLPixelFormatRGBA16Float ? 8 : 4) > 536870912ull) {
    error = "Invalid presentation cache extent or format"; return nil;
  }
  if (image_ && image_.width == width && image_.height == height && image_.pixelFormat == format)
    return image_;
  auto descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
      width:width height:height mipmapped:NO];
  descriptor.storageMode = MTLStorageModePrivate;
  descriptor.hazardTrackingMode = MTLHazardTrackingModeTracked;
  descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
  auto replacement = [context_->device newTextureWithDescriptor:descriptor];
  if (!replacement) { error = "Presentation cache allocation failed"; return nil; }
  replacement.label = @"Liberty processed game image";
  policy.InvalidatePixels();
  image_ = replacement;
  return image_;
}

bool PresentationImageCache::Draw(id<MTLRenderCommandEncoder> encoder,
                                  id<MTLTexture> destination, std::string& error) const {
  error.clear();
  if (!encoder || !image_ || !destination || image_ == destination ||
      destination.width != image_.width || destination.height != image_.height ||
      destination.pixelFormat != image_.pixelFormat || !context_) {
    error = "Invalid presentation cache copy"; return false;
  }
  const size_t index = destination.pixelFormat == MTLPixelFormatRGBA16Float ? 1 : 0;
  [encoder setRenderPipelineState:pipelines_[index]];
  [encoder setCullMode:MTLCullModeNone];
  [encoder setViewport:MTLViewport{0, 0, double(destination.width), double(destination.height), 0, 1}];
  [encoder setScissorRect:MTLScissorRect{0, 0, destination.width, destination.height}];
  [encoder setFragmentTexture:image_ atIndex:0];
  [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
  return true;
}

}  // namespace rex::ui::metal
