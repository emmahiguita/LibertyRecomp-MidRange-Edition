#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <rex/graphics/gta4_native/title_commands.h>

namespace rex::graphics::gta4_metal {
// Matches the existing resolve_convert/packed_depth_alias shader interface.
struct ResolveConstants {
  std::array<int32_t, 2> source_origin{};
  std::array<int32_t, 2> destination_origin{};
  uint32_t source_guest_sample_type = 0;
  uint32_t requested_guest_sample_type = 0;
  uint32_t destination_guest_sample_type = 0;
  uint32_t sample_select = 0;
  uint32_t mode = 0;
  uint32_t physical_source_sample_type = 0;
  uint32_t physical_destination_sample_type = 0;
  uint32_t flags = 0;
  std::array<uint32_t, 2> source_extent{};
  std::array<uint32_t, 2> destination_extent{};
};
static_assert(sizeof(ResolveConstants) == 64);
static_assert(offsetof(ResolveConstants, source_extent) == 48);

inline constexpr uint32_t kContentColor = 1;
inline constexpr uint32_t kContentDepth = 2;
inline constexpr uint32_t kContentStencil = 4;

constexpr uint32_t SampleType(uint32_t samples) {
  return samples == 4 ? 2 : samples == 2 ? 1 : 0;
}
constexpr uint32_t ScaleFloor(uint32_t value, uint32_t logical, uint32_t physical) {
  return logical ? uint32_t(uint64_t(value) * physical / logical) : 0;
}
constexpr uint32_t ScaleCeil(uint32_t value, uint32_t logical, uint32_t physical) {
  return logical ? uint32_t((uint64_t(value) * physical + logical - 1) / logical) : 0;
}
struct PixelRectangle {
  uint32_t x = 0, y = 0, width = 0, height = 0;
  bool full(uint32_t w, uint32_t h) const {
    return x == 0 && y == 0 && width == w && height == h;
  }
};
inline PixelRectangle ScaleRectangle(gta4_native::ResolveRectangle requested,
                                    uint32_t logical_width, uint32_t logical_height,
                                    uint32_t physical_width, uint32_t physical_height) {
  if (!logical_width || !logical_height || logical_width > INT32_MAX || logical_height > INT32_MAX)
    return {};
  const uint32_t left = uint32_t(std::clamp(requested.left, 0, int32_t(logical_width)));
  const uint32_t top = uint32_t(std::clamp(requested.top, 0, int32_t(logical_height)));
  const uint32_t right = uint32_t(std::clamp(requested.right, int32_t(left), int32_t(logical_width)));
  const uint32_t bottom = uint32_t(std::clamp(requested.bottom, int32_t(top), int32_t(logical_height)));
  const uint32_t x = ScaleFloor(left, logical_width, physical_width);
  const uint32_t y = ScaleFloor(top, logical_height, physical_height);
  return {x, y, ScaleCeil(right, logical_width, physical_width) - x,
          ScaleCeil(bottom, logical_height, physical_height) - y};
}
}  // namespace rex::graphics::gta4_metal
