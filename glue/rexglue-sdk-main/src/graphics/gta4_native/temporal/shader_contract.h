#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rex::graphics::gta4_native::temporal {
// Shader ABI version 1. Addresses refer to immutable GPU-visible uploads which
// remain alive through the last submission that consumes the corresponding draw.
struct TemporalDrawParameters {
  std::array<float,2> input_extent{}, jitter_clip{}, previous_jitter_clip{};
  uint32_t valid_history=0,reactive=0,ui_mode=0,padding[3]{};
};
static_assert(sizeof(TemporalDrawParameters)==48);
static_assert(offsetof(TemporalDrawParameters,valid_history)==24);
struct TemporalPushConstants {
  uint64_t vertex_constants=0,pixel_constants=0,shared_constants=0;
  uint64_t previous_vertex_constants=0,previous_shared_constants=0,draw_parameters=0;
};
static_assert(sizeof(TemporalPushConstants)==48);
static_assert(offsetof(TemporalPushConstants,previous_vertex_constants)==24);
static_assert(offsetof(TemporalPushConstants,previous_shared_constants)==32);
static_assert(offsetof(TemporalPushConstants,draw_parameters)==40);
inline constexpr uint32_t kMotionAttachment=4,kReactiveAttachment=5,kPreviousDepthAttachment=6;
inline constexpr uint32_t kCurrentClipLocation=18,kPreviousClipLocation=19;
}  // namespace rex::graphics::gta4_native::temporal
