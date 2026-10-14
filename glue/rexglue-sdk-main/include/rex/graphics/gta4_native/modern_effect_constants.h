#pragma once
#include <algorithm>
#include "environmental_data.h"
#include <cstddef>
#include <cstdint>

namespace rex::graphics::gta4_native {
// Appended to both native shared-constant layouts. Existing shader offsets stay fixed.
struct ModernEffectConstants {
  uint64_t tone_lut_address = 0;
  uint32_t enabled = 0;
  uint32_t reserved = 0;
  float azimuth_color_height[4]{};
  float azimuth_east_strength[4]{};
  float sky_color_exposure[4]{};
  float sun_color_exposure[4]{};
  float sun_direction[4]{};
  float sun_shafts_intensity = 0;
  float padding[3]{};
  uint64_t cloud_mask_address = 0;
  uint32_t cloud_mask_width = 0, cloud_mask_height = 0;
  float inverse_view_projection[16]{};
  float viewport[4]{};
  float depth_range[4]{};
};
inline constexpr size_t kModernEffectConstantsOffset = 0x420;
static_assert(sizeof(ModernEffectConstants) == 224);
static_assert(offsetof(ModernEffectConstants, azimuth_color_height) == 16);
static_assert(offsetof(ModernEffectConstants, sun_direction) == 80);
inline ModernEffectConstants BuildModernEffectConstants(bool enabled, uint64_t lut,
                                                        const EnvironmentalDataV2* environment) {
  ModernEffectConstants result{};
  result.enabled = enabled;
  result.tone_lut_address = enabled ? lut : 0;
  if (environment) {
    std::copy(environment->azimuth_color_height.begin(), environment->azimuth_color_height.end(), result.azimuth_color_height);
    std::copy(environment->azimuth_east_strength.begin(), environment->azimuth_east_strength.end(), result.azimuth_east_strength);
    std::copy(environment->sky_color_exposure.begin(), environment->sky_color_exposure.end(), result.sky_color_exposure);
    std::copy(environment->sun_color.begin(), environment->sun_color.end(), result.sun_color_exposure);
    std::copy(environment->sun_direction.begin(), environment->sun_direction.end(), result.sun_direction);
    result.sun_shafts_intensity = environment->sun_shafts_intensity;
    std::copy(environment->inverse_view_projection_matrix.begin(), environment->inverse_view_projection_matrix.end(), result.inverse_view_projection);
  }
  return result;
}
}  // namespace rex::graphics::gta4_native
