#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

#include "frame_input.h"

namespace rex::graphics::gta4_native::temporal {
// NVIDIA DLSS Programming Guide, 3.7: Halton(2,3), with at least eight
// samples per output-pixel area. Use the larger rounded axis ratio.
inline std::optional<std::array<float, 2>> ReconstructionJitter(
    uint64_t sequence, Extent render, Extent output) {
  if (!render || !output || render.width > output.width || render.height > output.height) return {};
  const double ratio = std::max(double(output.width) / render.width,
                                double(output.height) / render.height);
  const double phase_count = std::ceil(8.0 * ratio * ratio);
  if (!std::isfinite(phase_count) || phase_count > UINT32_MAX) return {};
  const uint64_t phase = sequence % uint64_t(phase_count) + 1;
  const auto halton = [](uint64_t index, uint32_t base) {
    double value = 0.0, fraction = 1.0;
    while (index) {
      fraction /= base;
      value += fraction * (index % base);
      index /= base;
    }
    return float(value - 0.5);
  };
  return std::array<float, 2>{halton(phase, 2), halton(phase, 3)};
}

// FSR and DLSS recommend preserving the title bias and adding
// log2(render/display)-1 for reconstruction of mipmapped scene materials.
inline float ReconstructionMipBias(const Configuration& config, bool material) {
  if (!material || !config.render_extent || !config.output_extent ||
      config.render_extent == config.output_extent) return 0.0f;
  const float ratio = std::min(float(config.render_extent.width) / config.output_extent.width,
                               float(config.render_extent.height) / config.output_extent.height);
  return std::log2(ratio) - 1.0f;
}
}  // namespace rex::graphics::gta4_native::temporal
