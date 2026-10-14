#pragma once
#import <Metal/Metal.h>
namespace rex::graphics::gta4_metal {
// Swizzle-only and linear/sRGB views need no PixelFormatView permission.
// Packed depth's independent stencil view does need component reinterpretation.
inline MTLTextureUsage NativeTextureUsage(bool attachment,bool depth_stencil=false,
                                         bool compute_write=false) {
  MTLTextureUsage usage=MTLTextureUsageShaderRead;
  if(attachment)usage|=MTLTextureUsageRenderTarget;
  if(depth_stencil)usage|=MTLTextureUsagePixelFormatView;
  if(compute_write)usage|=MTLTextureUsageShaderWrite;
  return usage;
}
}
