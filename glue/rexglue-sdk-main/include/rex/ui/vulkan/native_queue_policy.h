#pragma once
#include <cstdint>
#include <span>
#include <vulkan/vulkan_core.h>

namespace rex::ui::vulkan {
// UI and presentation stay on their original queue. Only the native offscreen
// renderer moves, so UI textures and command pools do not change ownership.
inline uint32_t SelectNativeOffscreenQueueFamily(
    std::span<const VkQueueFamilyProperties> families, uint32_t graphics,
    bool enabled, bool timeline_supported, bool moltenvk) {
  if (!enabled || !timeline_supported || !moltenvk || graphics >= families.size())
    return graphics;
  constexpr VkQueueFlags required = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
  for (uint32_t family = 0; family < families.size(); ++family) {
    if (family != graphics && families[family].queueCount &&
        (families[family].queueFlags & required) == required) return family;
  }
  return graphics;
}
}  // namespace rex::ui::vulkan
