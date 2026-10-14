#pragma once

#include "../gta4_native/core/geometry.h"
#include <array>
#include <cmath>
#include <span>

namespace rex::graphics::gta4_metal {

// Xenos supplies three corners for each rectangle. Mirror the maintained
// Vulkan UP path (graphics_system.cpp, RecordDrawPrimitiveUp), then emit two
// independent triangles. A normal three-vertex Metal strip only covers half.
// Input contains host-endian vertices; output may be write-combined memory.
inline bool ExpandRectangleList(std::span<const uint8_t> input, uint32_t count,
    uint32_t stride, std::span<const gta4_native::VertexElement> elements,
    std::span<uint8_t> output) {
  using namespace gta4_native::core;
  if (!count || count % 3 || !stride || uint64_t(count) * stride > input.size() ||
      uint64_t(count) * 2 * stride > output.size()) return false;
  const gta4_native::VertexElement* position = nullptr;
  for (const auto& element : elements) {
    if (element.stream == 0 && ConvertVertexUsageToLocation(element.usage, element.usage_index) == 0 &&
        GetFloat32VertexElementComponentCount(element.type) >= 2 &&
        size_t(element.offset) + 2 * sizeof(float) <= stride) {
      position = &element;
      break;
    }
  }
  for (uint32_t rectangle = 0; rectangle < count / 3; ++rectangle) {
    const std::array<const uint8_t*, 3> supplied{
        input.data() + size_t(rectangle * 3) * stride,
        input.data() + size_t(rectangle * 3 + 1) * stride,
        input.data() + size_t(rectangle * 3 + 2) * stride};
    uint32_t first = 0;
    if (position) {
      std::array<std::array<float, 2>, 3> coordinates{};
      for (size_t i = 0; i < supplied.size(); ++i) {
        std::memcpy(coordinates[i].data(), supplied[i] + position->offset, sizeof(coordinates[i]));
        if (!std::isfinite(coordinates[i][0]) || !std::isfinite(coordinates[i][1])) return false;
      }
      float longest = -1;
      for (uint32_t candidate = 0; candidate < 3; ++candidate) {
        const auto& a = coordinates[(candidate + 1) % 3];
        const auto& b = coordinates[(candidate + 2) % 3];
        const float dx = a[0] - b[0], dy = a[1] - b[1];
        const float length = dx * dx + dy * dy;
        if (length > longest) { longest = length; first = candidate; }
      }
    }
    const auto* a = supplied[first];
    const auto* b = supplied[(first + 1) % 3];
    const auto* c = supplied[(first + 2) % 3];
    auto* destination = output.data() + size_t(rectangle * 6) * stride;
    const std::array<const uint8_t*, 6> triangles{a, b, c, c, b, c};
    for (size_t i = 0; i < triangles.size(); ++i)
      std::memcpy(destination + i * stride, triangles[i], stride);
    auto* fourth = destination + size_t(5) * stride;
    for (const auto& element : elements) {
      if (element.stream != 0) continue;
      const uint32_t components = GetFloat32VertexElementComponentCount(element.type);
      if (!components || size_t(element.offset) + components * sizeof(float) > stride) continue;
      for (uint32_t component = 0; component < components; ++component) {
        const size_t offset = element.offset + component * sizeof(float);
        float va, vb, vc;
        std::memcpy(&va, a + offset, sizeof(va));
        std::memcpy(&vb, b + offset, sizeof(vb));
        std::memcpy(&vc, c + offset, sizeof(vc));
        const float reconstructed = vb - va + vc;
        std::memcpy(fourth + offset, &reconstructed, sizeof(reconstructed));
      }
    }
  }
  return true;
}

}  // namespace rex::graphics::gta4_metal
