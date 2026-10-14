#pragma once
#include <array>
#include <cstdint>
namespace rex::graphics::gta4_metal {
struct PendingClear {
  uint32_t aspects = 0;
  std::array<float, 4> color{};
  float depth = 0;
  uint32_t stencil = 0;
  void Merge(uint32_t requested, bool depth_surface, bool was_initialized,
             const std::array<float, 4>& next_color, float next_depth, uint32_t next_stencil) {
    // A first depth-only (or stencil-only) clear also defines the other aspect.
    if (!was_initialized && depth_surface) {
      if (!(aspects & 2u)) depth = 0;
      if (!(aspects & 4u)) stencil = 0;
      aspects |= 6u;
    }
    if (requested & 1u) color = next_color;
    if (requested & 2u) depth = next_depth;
    if (requested & 4u) stencil = next_stencil & 255u;
    aspects |= requested;
  }
};
}  // namespace rex::graphics::gta4_metal
