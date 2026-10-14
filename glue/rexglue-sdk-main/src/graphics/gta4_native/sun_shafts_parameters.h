#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <rex/graphics/gta4_native/environmental_data.h>

namespace rex::graphics::gta4_native {
struct SunShaftParameters {
  std::array<float, 2> screen_position{};
  std::array<float, 4> view_sun_projection{}, depth_projection{};
  float intensity = 0, horizon_fade = 0;
  uint64_t cloud_mask_address = 0;
  uint32_t cloud_width = 0, cloud_height = 0;
  bool valid = false;
};
// Shared with sun_shafts_ps.glsl and its compiled Metal entry point.
struct SunShaftPushConstants {
  int32_t source_extent[2], destination_extent[2];
  uint32_t pass_index, sample_count;
  float density, decay;
  float sun_screen[4];
  float view_sun_projection[4];
  float depth_projection[4];
  uint64_t cloud_mask_address;
  uint32_t cloud_width, cloud_height;
};
static_assert(sizeof(SunShaftPushConstants) == 96);

inline SunShaftParameters BuildSunShaftParameters(const EnvironmentalDataV2* data,
                                                  const std::array<float, 4>& dof) {
  SunShaftParameters result;
  constexpr uint64_t required = EnvironmentalFieldBit(EnvironmentalField::kSunDirection) |
      EnvironmentalFieldBit(EnvironmentalField::kViewProjectionMatrix) |
      EnvironmentalFieldBit(EnvironmentalField::kViewMatrix) |
      EnvironmentalFieldBit(EnvironmentalField::kProjectionMatrix) |
      EnvironmentalFieldBit(EnvironmentalField::kCameraAltitude) |
      EnvironmentalFieldBit(EnvironmentalField::kSunShaftIntensity);
  if (!data || (data->valid_fields & required) != required ||
      !ValidEnvironmentalData(*data) || !std::isfinite(dof[0]) || !std::isfinite(dof[1]) ||
      dof[0] <= 0 || dof[1] <= dof[0] || data->sun_shafts_intensity <= 0) return result;
  const auto& direction = data->sun_direction;
  std::array<float, 4> clip{};
  for (size_t column = 0; column < 4; ++column) {
    for (size_t row = 0; row < 3; ++row) {
      clip[column] += direction[row] * data->view_projection_matrix[row * 4 + column];
      result.view_sun_projection[column] += direction[row] * data->view_matrix[row * 4 + column];
    }
  }
  if (!std::isfinite(clip[3]) || clip[3] <= 1.0e-6f ||
      std::abs(data->projection_matrix[0]) <= 1.0e-6f ||
      std::abs(data->projection_matrix[5]) <= 1.0e-6f) return {};
  result.screen_position = {clip[0] / clip[3] * 0.5f + 0.5f,
                            -clip[1] / clip[3] * 0.5f + 0.5f};
  if (!std::isfinite(result.screen_position[0]) || !std::isfinite(result.screen_position[1])) return {};
  result.view_sun_projection[3] = data->projection_matrix[0];
  constexpr uint64_t fog_fields = EnvironmentalFieldBit(EnvironmentalField::kFogDensity) |
      EnvironmentalFieldBit(EnvironmentalField::kFogHeightFalloff) |
      EnvironmentalFieldBit(EnvironmentalField::kFogAltitudeTweak) |
      EnvironmentalFieldBit(EnvironmentalField::kFogPower);
  const bool fog = (data->valid_fields & fog_fields) == fog_fields;
  result.depth_projection = {data->projection_matrix[5], dof[0], dof[1], fog ? 1.0f : 0.0f};
  const float azimuth = std::clamp(direction[2] / 0.209101f, 0.0f, 1.0f);
  const float horizon = azimuth * azimuth * (3.0f - 2.0f * azimuth);
  const float altitude = 1.0f - std::exp2(-0.01f * std::max(data->camera_altitude, 0.0f));
  result.horizon_fade = fog ? std::max(horizon, altitude) : 1.0f;
  result.intensity = data->sun_shafts_intensity;
  result.valid = result.horizon_fade > 0;
  return result;
}

inline SunShaftPushConstants BuildSunShaftPushConstants(const SunShaftParameters& p,
    uint32_t sw, uint32_t sh, uint32_t dw, uint32_t dh, uint32_t pass) {
  SunShaftPushConstants result{};
  result.source_extent[0] = int32_t(sw); result.source_extent[1] = int32_t(sh);
  result.destination_extent[0] = int32_t(dw); result.destination_extent[1] = int32_t(dh);
  result.pass_index = pass; result.sample_count = 24;
  result.density = 0.9f; result.decay = 0.95f;
  result.sun_screen[0] = p.screen_position[0]; result.sun_screen[1] = p.screen_position[1];
  result.sun_screen[2] = p.intensity; result.sun_screen[3] = p.horizon_fade;
  std::copy(p.view_sun_projection.begin(), p.view_sun_projection.end(), result.view_sun_projection);
  std::copy(p.depth_projection.begin(), p.depth_projection.end(), result.depth_projection);
  result.cloud_mask_address = p.cloud_mask_address;
  result.cloud_width = p.cloud_width; result.cloud_height = p.cloud_height;
  return result;
}
}  // namespace rex::graphics::gta4_native
