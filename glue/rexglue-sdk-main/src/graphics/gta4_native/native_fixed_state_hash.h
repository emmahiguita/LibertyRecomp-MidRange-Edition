#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <xxhash.h>
namespace rex::graphics::gta4_native {
// Explicitly serialize all captured members. Never hash C++ padding; keep exact
// floating-point bit patterns for the capture/finalize/record integrity check.
template <typename State>
inline uint64_t HashNativeFixedFunctionState(const State& state) {
  std::array<uint8_t, sizeof(State)> bytes{};
  size_t used = 0;
  const auto add = [&](const auto& value) {
    std::memcpy(bytes.data() + used, &value, sizeof(value));
    used += sizeof(value);
  };
  add(state.depth_enable);
  add(state.depth_function);
  add(state.depth_write_enable);
  add(state.depth_clamp_enable);
  add(state.clip_control);
  add(state.user_clip_plane_enable_mask);
  add(state.clip_plane_bits);
  add(state.negative_one_to_one_clip_space);
  add(state.cull_mode);
  add(state.polygon_mode);
  add(state.blend_enable);
  add(state.blend_controls);
  add(state.source_blend);
  add(state.destination_blend);
  add(state.blend_operation);
  add(state.source_blend_alpha);
  add(state.destination_blend_alpha);
  add(state.blend_operation_alpha);
  add(state.blend_constants);
  add(state.alpha_test_enable);
  add(state.alpha_function);
  add(state.alpha_reference);
  add(state.alpha_to_mask_enable);
  add(state.alpha_to_mask);
  add(state.stencil_enable);
  add(state.two_sided_stencil);
  add(state.stencil_fail);
  add(state.stencil_depth_fail);
  add(state.stencil_pass);
  add(state.stencil_function);
  add(state.stencil_reference);
  add(state.stencil_mask);
  add(state.stencil_write_mask);
  add(state.back_stencil_reference);
  add(state.back_stencil_mask);
  add(state.back_stencil_write_mask);
  add(state.ccw_stencil_fail);
  add(state.ccw_stencil_depth_fail);
  add(state.ccw_stencil_pass);
  add(state.ccw_stencil_function);
  add(state.scissor_enable);
  add(state.slope_scaled_depth_bias_bits);
  add(state.depth_bias_bits);
  add(state.depth_bias_enable);
  add(state.depth_bias_representable);
  add(state.color_write_mask);
  add(state.sample_mask);
  add(state.viewport_bits);
  add(state.scissor);
  return XXH3_64bits_withSeed(bytes.data(), used, 0);
}
// Values excluded here never select a compiled pipeline. Keep blend constants
// and the front/back stencil relationship: portability validation reads them.
// Do not modify the draw itself; dynamic binding still uses its complete state.
template <typename State>
inline State NativePipelineMemoState(State state) {
  state.viewport_bits = {};
  state.scissor = {};
  state.scissor_enable = 0;
  state.alpha_reference = 0;
  state.slope_scaled_depth_bias_bits = 0;
  state.depth_bias_bits = 0;
  // The numeric reference is dynamic; equality of the two faces can affect
  // representability on devices without separate stencil mask/reference.
  const bool references_equal = state.stencil_reference == state.back_stencil_reference;
  state.stencil_reference = 0;
  state.back_stencil_reference = references_equal ? 0u : 1u;
  return state;
}
}  // namespace rex::graphics::gta4_native
