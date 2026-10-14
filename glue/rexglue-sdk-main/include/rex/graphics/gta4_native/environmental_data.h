#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>

namespace rex::graphics::gta4_native {

inline constexpr uint32_t kEnvironmentalDataVersion = 2;

enum class EnvironmentalField : uint64_t {
  kTimeStepSeconds = 0x0000000000000001,
  kMotionBlurScale = 0x0000000000000002,
  kDirectionalMotionBlurLength = 0x0000000000000004,
  kFogStart = 0x0000000000000008,
  kFogDensity = 0x0000000000000010,
  kFogHeightFalloff = 0x0000000000000020,
  kFogAltitudeTweak = 0x0000000000000040,
  kFogPower = 0x0000000000000080,
  kFogColor = 0x0000000000000100,
  kSunDirection = 0x0000000000000200,
  kSunColor = 0x0000000000000400,
  kViewMatrix = 0x0000000000000800,
  kViewInverseMatrix = 0x0000000000001000,
  kProjectionMatrix = 0x0000000000002000,
  kViewProjectionMatrix = 0x0000000000004000,
  kCameraPosition = 0x0000000000008000,
  kCameraAltitude = 0x0000000000010000,
  kEffectSettings = 0x0000000000020000,
  kAzimuthColorHeight = 0x0000000000040000,
  kAzimuthEastStrength = 0x0000000000080000,
  kSkyColorExposure = 0x0000000000100000,
  kSunShaftIntensity = 0x0000000000200000,
  kFogFarClip = 0x0000000000400000,
  kInverseViewProjectionMatrix = 0x0000000000800000,
};

inline constexpr uint64_t kEnvironmentalFieldMask = 0x0000000000FFFFFF;

constexpr uint64_t EnvironmentalFieldBit(EnvironmentalField field) {
  return static_cast<uint64_t>(field);
}

struct EnvironmentalEffectSettingsV1 {
  uint32_t enabled_effects = 0;
  uint32_t motion_blur_quality = 0;
};

// Matrices preserve the guest's row-major float[4][4] storage. Consumers must
// perform an explicit convention conversion when binding them to shader blocks.
struct alignas(16) EnvironmentalDataV2 {
  uint32_t version = kEnvironmentalDataVersion;
  uint32_t byte_size = 0;
  uint64_t valid_fields = 0;
  uint64_t source_sequence = 0;

  float time_step_seconds = 0.0f;
  float motion_blur_scale = 0.0f;
  float directional_motion_blur_length = 0.0f;
  float fog_start = 0.0f;
  float fog_density = 0.0f;
  float fog_height_falloff = 0.0f;
  float fog_altitude_tweak = 0.0f;
  float fog_power = 0.0f;
  float camera_altitude = 0.0f;

  std::array<float, 4> fog_color{};
  std::array<float, 4> sun_direction{};
  std::array<float, 4> sun_color{};
  std::array<float, 4> camera_position{};

  std::array<float, 16> view_matrix{};
  std::array<float, 16> view_inverse_matrix{};
  std::array<float, 16> projection_matrix{};
  std::array<float, 16> view_projection_matrix{};

  EnvironmentalEffectSettingsV1 effect_settings{};
  std::array<uint32_t, 3> reserved{};
  std::array<float, 4> azimuth_color_height{};
  std::array<float, 4> azimuth_east_strength{};
  std::array<float, 4> sky_color_exposure{};
  float sun_shafts_intensity = 0.0f;
  float fog_far_clip = 0.0f;
  std::array<uint32_t, 2> reserved_v2{};
  std::array<float, 16> inverse_view_projection_matrix{};
};

static_assert(std::is_trivially_copyable_v<EnvironmentalEffectSettingsV1>);
static_assert(std::is_trivially_copyable_v<EnvironmentalDataV2>);
static_assert(alignof(EnvironmentalDataV2) == 16);
static_assert(sizeof(EnvironmentalDataV2) == 528);

// Row-major Gauss-Jordan inversion, evaluated only when capturing a viewport.
inline bool InvertEnvironmentalMatrix(const std::array<float, 16>& input,
                                      std::array<float, 16>& output) {
  double rows[4][8]{};
  for (size_t r = 0; r < 4; ++r) {
    for (size_t c = 0; c < 4; ++c) rows[r][c] = input[r * 4 + c];
    rows[r][r + 4] = 1;
  }
  for (size_t c = 0; c < 4; ++c) {
    size_t pivot = c;
    for (size_t r = c + 1; r < 4; ++r)
      if (std::abs(rows[r][c]) > std::abs(rows[pivot][c])) pivot = r;
    if (!std::isfinite(rows[pivot][c]) || std::abs(rows[pivot][c]) < 1.0e-12) return false;
    for (size_t i = 0; i < 8; ++i) std::swap(rows[c][i], rows[pivot][i]);
    const double scale = rows[c][c];
    for (double& value : rows[c]) value /= scale;
    for (size_t r = 0; r < 4; ++r) {
      if (r == c) continue;
      const double factor = rows[r][c];
      for (size_t i = 0; i < 8; ++i) rows[r][i] -= factor * rows[c][i];
    }
  }
  for (size_t r = 0; r < 4; ++r)
    for (size_t c = 0; c < 4; ++c) {
      output[r * 4 + c] = float(rows[r][c + 4]);
      if (!std::isfinite(output[r * 4 + c])) return false;
    }
  return true;
}

inline bool ValidEnvironmentalData(const EnvironmentalDataV2& data) {
  if (data.version != kEnvironmentalDataVersion || data.byte_size != sizeof(data) ||
      !data.source_sequence || (data.valid_fields & ~kEnvironmentalFieldMask)) return false;
    auto field_is_valid = [&data](EnvironmentalField field) {
      return (data.valid_fields & EnvironmentalFieldBit(field)) != 0;
    };
    auto finite_scalar = [&field_is_valid](EnvironmentalField field, float value) {
      return !field_is_valid(field) || std::isfinite(value);
    };
    auto finite_array = [&field_is_valid](EnvironmentalField field, const auto& values) {
      return !field_is_valid(field) || std::all_of(values.begin(), values.end(), [](float value) {
        return std::isfinite(value);
      });
    };
    if (!finite_scalar(EnvironmentalField::kTimeStepSeconds, data.time_step_seconds) ||
        (field_is_valid(EnvironmentalField::kTimeStepSeconds) && data.time_step_seconds < 0.0f) ||
        !finite_scalar(EnvironmentalField::kMotionBlurScale, data.motion_blur_scale) ||
        !finite_scalar(EnvironmentalField::kDirectionalMotionBlurLength,
                       data.directional_motion_blur_length) ||
        !finite_scalar(EnvironmentalField::kFogStart, data.fog_start) ||
        !finite_scalar(EnvironmentalField::kFogDensity, data.fog_density) ||
        !finite_scalar(EnvironmentalField::kFogHeightFalloff, data.fog_height_falloff) ||
        !finite_scalar(EnvironmentalField::kFogAltitudeTweak, data.fog_altitude_tweak) ||
        !finite_scalar(EnvironmentalField::kFogPower, data.fog_power) ||
        !finite_scalar(EnvironmentalField::kCameraAltitude, data.camera_altitude) ||
        !finite_scalar(EnvironmentalField::kSunShaftIntensity, data.sun_shafts_intensity) ||
        !finite_scalar(EnvironmentalField::kFogFarClip, data.fog_far_clip) ||
        !finite_array(EnvironmentalField::kAzimuthColorHeight, data.azimuth_color_height) ||
        !finite_array(EnvironmentalField::kAzimuthEastStrength, data.azimuth_east_strength) ||
        !finite_array(EnvironmentalField::kSkyColorExposure, data.sky_color_exposure) ||
        !finite_array(EnvironmentalField::kInverseViewProjectionMatrix, data.inverse_view_projection_matrix) ||
        !finite_array(EnvironmentalField::kFogColor, data.fog_color) ||
        !finite_array(EnvironmentalField::kSunDirection, data.sun_direction) ||
        !finite_array(EnvironmentalField::kSunColor, data.sun_color) ||
        !finite_array(EnvironmentalField::kCameraPosition, data.camera_position) ||
        !finite_array(EnvironmentalField::kViewMatrix, data.view_matrix) ||
        !finite_array(EnvironmentalField::kViewInverseMatrix, data.view_inverse_matrix) ||
        !finite_array(EnvironmentalField::kProjectionMatrix, data.projection_matrix) ||
        !finite_array(EnvironmentalField::kViewProjectionMatrix, data.view_projection_matrix)) {
      return false;
    }
  return true;
}
}  // namespace rex::graphics::gta4_native
