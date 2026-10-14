#pragma once

#include <cstdint>
#include <optional>

#include "../../gta4_native/modern_shader_policy.h"
#include "../../gta4_native/core/draw_state.h"

namespace rex::graphics::gta4_metal::temporal {
enum class CompositeFilter { kNone, kStockF6 };
struct CompositeContract {
  uint32_t scene_stage;
  CompositeFilter filter;
};

// Scene reconstruction eligibility is separate from modern DoF/sun shafts.
// F6 binds scene HDR at stage 1 and adapted luminance at stage 2. Its stock
// neighbor-selection filter must run on the original grid before reconstruction.
constexpr std::optional<CompositeContract> CompositeForShader(uint64_t hash) {
  if (hash == 0xF6AEB9A606561C54ull)
    return CompositeContract{1, CompositeFilter::kStockF6};
  if (gta4_native::SupportsSplitPostFx(hash))
    return CompositeContract{2, CompositeFilter::kNone};
  return std::nullopt;
}

// A scene-linear scratch is an intermediate value, not the guest framebuffer.
// Preserve the title's raster geometry/viewport but write every channel without
// blending, depth/stencil rejection or an alpha test intended for the final draw.
inline gta4_native::core::FixedFunctionState LinearCompositeState(
    const gta4_native::core::FixedFunctionState& original) {
  auto state = original;
  state.depth_enable = state.depth_write_enable = state.stencil_enable = 0;
  state.alpha_test_enable = state.alpha_to_mask_enable = state.alpha_to_mask = 0;
  state.blend_enable = 0;
  state.blend_controls.fill(0x00010001u);
  state.color_write_mask = 0xFu;
  return state;
}
}  // namespace rex::graphics::gta4_metal::temporal
