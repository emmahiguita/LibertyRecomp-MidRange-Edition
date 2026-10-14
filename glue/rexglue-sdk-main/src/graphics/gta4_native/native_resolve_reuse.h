#pragma once
#include <array>
#include <cstdint>
#include <vulkan/vulkan_core.h>

namespace rex::graphics::gta4_native {

// An equality proof for a repeated color resolve within one command recording.
// No GPU ownership or image storage is retained. In particular this is not a
// copy-to-conversion substitution: the operation and numeric contract are keys.
struct NativeResolveReuseKey {
  uint64_t recording = 0;
  uint64_t source_image = 0, source_view = 0, source_lifetime = 0, source_writer = 0;
  uint64_t destination_image = 0, destination_lifetime = 0, destination_generation = 0;
  uint32_t operation = 0, source_format = 0, destination_format = 0;
  uint32_t physical_samples = 0, content_samples = 0, requested_samples = 0;
  uint32_t sample_select = 0, flags = 0, mip = 0;
  int32_t exponent = 0;
  std::array<int32_t, 2> source_origin{}, destination_origin{};
  std::array<uint32_t, 2> source_extent{}, destination_extent{};
  std::array<uint32_t, 2> source_image_extent{}, destination_image_extent{};
  bool operator==(const NativeResolveReuseKey&) const = default;
  bool valid() const {
    return recording && recording != UINT64_MAX && source_image && source_lifetime && source_writer &&
           destination_image && destination_lifetime && destination_generation &&
           source_image != destination_image && source_extent[0] && source_extent[1] &&
           destination_extent[0] && destination_extent[1];
  }
};

struct NativeResolveReuseRecord {
  NativeResolveReuseKey key{};
  uint64_t destination_writer = 0;
  bool Matches(const NativeResolveReuseKey& request, uint64_t current_destination_writer) const {
    return request.valid() && current_destination_writer &&
           destination_writer == current_destination_writer && key == request;
  }
  void Commit(const NativeResolveReuseKey& request, uint64_t writer) {
    key = request;
    destination_writer = writer;
  }
};

// Both outputs of the production programmable resolve apply the identical
// sanitize_float16_color operation when the ordinary target is already FP16.
// The ordinary image is then the high-precision result; a second image is redundant.
constexpr bool CanUseNativeResolvedColorAsHDRMirror(VkFormat format, VkImageAspectFlags aspects,
                                                   VkSampleCountFlagBits samples, uint32_t mips) {
  return format == VK_FORMAT_R16G16B16A16_SFLOAT && aspects == VK_IMAGE_ASPECT_COLOR_BIT &&
         samples == VK_SAMPLE_COUNT_1_BIT && mips == 1;
}
}  // namespace rex::graphics::gta4_native
