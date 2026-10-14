#pragma once
#include <array>
#include <cstdint>
#include "native_postfx_plan.h"

namespace rex::graphics::gta4_native {
enum class PostFxDepthSource : uint32_t { kCurrentCompositeTexture, kPreAlphaTexture };
struct SplitPostFxParameters {
  std::array<float, 4> dof_projection{}, dof_distance{}, dof_blur{};
  PostFxDepthSource depth_source = PostFxDepthSource::kCurrentCompositeTexture;
};
// Identical ABI for Vulkan push constants and the generated Metal buffer(0).
struct SplitPostFxPushConstants {
  int32_t source_extent[2], destination_extent[2];
  uint32_t pass_index, depth_source;
  int32_t full_extent[2];
  float dof_projection[4], dof_distance[4], dof_blur[4];
};
static_assert(sizeof(SplitPostFxPushConstants) == 80);
inline bool ValidSplitPostFxParameters(const SplitPostFxParameters& p) {
  const auto finite = [](const auto& values) {
    return std::all_of(values.begin(), values.end(), [](float x) { return std::isfinite(x); });
  };
  if (!finite(p.dof_projection) || !finite(p.dof_distance) || !finite(p.dof_blur))
    return false;
  if (NativeDofCanBeElided(p.dof_projection, p.dof_distance, p.dof_blur))
    return true;
  // c209 is the Xbox reciprocal projection, not FusionFix PC's logarithmic encoding.
  return p.dof_projection[0] > 0 && p.dof_projection[1] > 0 && p.dof_distance[0] != 0 &&
         p.dof_distance[2] != 0;
}
}  // namespace rex::graphics::gta4_native
