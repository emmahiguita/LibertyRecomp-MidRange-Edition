#include "presentation_effects.h"
#include "context.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace rex::ui::metal {
bool PresentationEffects::Initialize(std::shared_ptr<MetalContext> context, std::string& error) {
  @autoreleasepool {
    error.clear();
    if (!context || !context->device || !context->ui_library) {
      error = "Presentation effects require a Metal device and UI library"; return false;
    }
    if (!context->pass_library) {
      NSURL* url = [[NSBundle mainBundle] URLForResource:@"passes" withExtension:@"metallib" subdirectory:@"metal"];
      if (!url) { error = "Bundle is missing metal/passes.metallib"; return false; }
      NSError* native_error = nil;
      context->pass_library = [context->device newLibraryWithURL:url error:&native_error];
      if (!context->pass_library) { error = MetalError(native_error, "Cannot load Metal utility passes"); return false; }
    }
    // Pipeline creation is confined to initialization, never the paint loop.
    constexpr const char* names[] = {
      "liberty_guest_output_bilinear_ps", "liberty_guest_output_bilinear_dither_ps",
      "liberty_guest_output_ffx_cas_sharpen_ps", "liberty_guest_output_ffx_cas_sharpen_dither_ps",
      "liberty_guest_output_ffx_cas_resample_ps", "liberty_guest_output_ffx_cas_resample_dither_ps",
      "liberty_guest_output_ffx_fsr_easu_ps", "liberty_guest_output_ffx_fsr_rcas_ps",
      "liberty_guest_output_ffx_fsr_rcas_dither_ps"
    };
    static_assert(std::size(names) == kEffectCount);
    const MTLPixelFormat formats[] = {MTLPixelFormatBGRA8Unorm, MTLPixelFormatRGBA16Float};
    auto vertex = [context->ui_library newFunctionWithName:@"liberty_present_vertex"];
    if (!vertex) { error = "Missing fullscreen vertex function"; return false; }
    for (size_t index = 0; index < kEffectCount; ++index) {
      auto pixel = [context->pass_library newFunctionWithName:[NSString stringWithUTF8String:names[index]]];
      if (!pixel) { error = std::string("Missing presentation function: ") + names[index]; return false; }
      for (size_t format = 0; format < std::size(formats); ++format) {
        auto descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = vertex;
        descriptor.fragmentFunction = pixel;
        descriptor.colorAttachments[0].pixelFormat = formats[format];
        descriptor.label = [NSString stringWithUTF8String:names[index]];
        NSError* native_error = nil;
        pipelines_[index][format] = [context->device newRenderPipelineStateWithDescriptor:descriptor error:&native_error];
        if (!pipelines_[index][format]) { error = MetalError(native_error, "Presentation pipeline creation failed"); return false; }
      }
    }
    auto sampler = [MTLSamplerDescriptor new];
    sampler.minFilter = sampler.magFilter = MTLSamplerMinMagFilterLinear;
    sampler.sAddressMode = sampler.tAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler_ = [context->device newSamplerStateWithDescriptor:sampler];
    if (!sampler_) { error = "Presentation sampler creation failed"; return false; }
    context_ = std::move(context);
    return true;
  }
}

bool PresentationEffects::Draw(id<MTLRenderCommandEncoder> encoder, PresentationEffect effect,
    id<MTLTexture> source, id<MTLTexture> target, MTLViewport viewport,
    std::span<const std::byte> constants, std::string& error) {
  error.clear();
  const size_t index = size_t(effect);
  const size_t format = target.pixelFormat == MTLPixelFormatBGRA8Unorm ? 0 : 1;
  if (!context_ || !encoder || !source || !target || source == target ||
      (target.pixelFormat != MTLPixelFormatBGRA8Unorm && target.pixelFormat != MTLPixelFormatRGBA16Float) ||
      index >= kEffectCount || constants.empty() || constants.size() > 48 ||
      !std::isfinite(viewport.originX) || !std::isfinite(viewport.originY) ||
      !std::isfinite(viewport.width) || !std::isfinite(viewport.height) ||
      viewport.width <= 0 || viewport.height <= 0) {
    error = "Invalid Metal presentation pass"; return false;
  }
  // Original fragment push constants start at byte 16, after the Vulkan
  // rectangle vertex constants. Preserve that offset in translated MSL.
  std::array<std::byte, 64> bytes{};
  std::memcpy(bytes.data() + 16, constants.data(), constants.size());
  const double left = std::clamp(viewport.originX, 0.0, double(target.width));
  const double top = std::clamp(viewport.originY, 0.0, double(target.height));
  const double right = std::clamp(viewport.originX + viewport.width, left, double(target.width));
  const double bottom = std::clamp(viewport.originY + viewport.height, top, double(target.height));
  if (right <= left || bottom <= top) return true;
  [encoder setRenderPipelineState:pipelines_[index][format]];
  [encoder setCullMode:MTLCullModeNone];
  [encoder setViewport:viewport];
  [encoder setScissorRect:MTLScissorRect{NSUInteger(left), NSUInteger(top), NSUInteger(right-left), NSUInteger(bottom-top)}];
  [encoder setFragmentTexture:source atIndex:0];
  [encoder setFragmentSamplerState:sampler_ atIndex:1];
  // MSL struct alignment includes trailing padding (for example CAS uses 32
  // bytes even though its original push payload occupies 28). Bind the complete
  // zero-initialized block rather than truncating it at the last scalar.
  [encoder setFragmentBytes:bytes.data() length:bytes.size() atIndex:0];
  [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
  return true;
}
}  // namespace rex::ui::metal
