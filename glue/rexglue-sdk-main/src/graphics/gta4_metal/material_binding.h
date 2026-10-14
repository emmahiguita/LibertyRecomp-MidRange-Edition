#pragma once
#include "resources.h"
#include <array>
namespace rex::graphics::gta4_metal {
// The named ResourceStore owns these objects. An epoch changes before any image
// can be replaced, erased or reinterpreted. No cache-held image extends residency.
struct MaterialBinding {
  uint64_t epoch=0;
  uint32_t handle=0;
  std::array<uint32_t,6> fetch{};
  TextureResource* resource=nullptr;
  __unsafe_unretained id<MTLTexture> image=nil;
  __unsafe_unretained id<MTLSamplerState> sampler=nil;
  uint64_t image_id=0,sampler_id=0;
  uint32_t heap=0,mip_levels=0;
};
}
