#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace rex::graphics::gta4_native {
struct FusionTimecycle {
  std::array<float, 5> values{};
};
#include "fusion_timecycle_data.inc"

inline bool SampleFusionTimecycle(float hours, uint32_t old_weather, uint32_t new_weather,
                                  float weather_blend, FusionTimecycle& result) {
  if (!std::isfinite(hours) || !std::isfinite(weather_blend) ||
      old_weather >= 9 || new_weather >= 9) return false;
  constexpr std::array<float, 12> knots{0, 5, 6, 7, 9, 12, 18, 19, 20, 21, 22, 24};
  hours = std::fmod(hours, 24.0f);
  if (hours < 0) hours += 24.0f;
  size_t h = 0;
  while (h < 10 && hours >= knots[h + 1]) ++h;
  const size_t next = (h + 1) % 11;
  const float time = (hours - knots[h]) / (knots[h + 1] - knots[h]);
  weather_blend = std::clamp(weather_blend, 0.0f, 1.0f);
  const std::array<float, 4> weights{(1-time)*(1-weather_blend), time*(1-weather_blend),
                                     (1-time)*weather_blend, time*weather_blend};
  const std::array<size_t, 4> indices{old_weather*11+h, old_weather*11+next,
                                      new_weather*11+h, new_weather*11+next};
  for (size_t field = 0; field < result.values.size(); ++field) {
    result.values[field] = 0;
    for (size_t i = 0; i < weights.size(); ++i)
      result.values[field] += kFusionTimecycles[indices[i]].values[field] * weights[i];
  }
  return true;
}

inline const FusionTimecycle* FindFusionModifier(uint32_t hash) {
  const auto it = std::lower_bound(kFusionModifiers.begin(), kFusionModifiers.end(), hash,
      [](const FusionModifier& entry, uint32_t key) { return entry.hash < key; });
  return it != kFusionModifiers.end() && it->hash == hash ? &it->values : nullptr;
}
inline void ApplyFusionModifier(FusionTimecycle& values, const FusionTimecycle& modifier,
                                 float weight) {
  if (!std::isfinite(weight) || weight <= 0) return;
  weight = std::min(weight, 1.0f);
  for (size_t i = 0; i < values.values.size(); ++i)
    if (modifier.values[i] >= 0)
      values.values[i] = values.values[i]*(1-weight) + modifier.values[i]*weight;
}
}  // namespace rex::graphics::gta4_native
