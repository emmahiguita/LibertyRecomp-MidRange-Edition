#pragma once
#include <array>
#include <cstdint>
namespace rex::graphics::gta4_metal {
struct ResolveReuseKey {
  uint64_t recording = 0, source_image = 0, source_generation = 0, source_writer = 0;
  uint64_t destination_image = 0, destination_generation = 0;
  uint32_t level = 0, slice = 0, source_format = 0, destination_format = 0, direct = 0;
  std::array<uint32_t, 16> conversion{};
  std::array<uint32_t, 4> image_extents{};
  bool operator==(const ResolveReuseKey&) const = default;
  bool valid() const {
    return recording && source_image && source_generation && source_writer &&
        destination_image && destination_generation && source_image != destination_image;
  }
};
struct ResolveReuseRecord {
  ResolveReuseKey key{};
  uint64_t writer = 0;
  bool Matches(const ResolveReuseKey& candidate, uint64_t current_writer) const {
    return candidate.valid() && writer && writer == current_writer && key == candidate;
  }
};
// A programmable first partial write can initialize untouched pixels in the
// same pass. A blit cannot, and must retain its separate initialization pass.
constexpr bool MergeResolveInitialization(bool direct, bool full, bool existing) {
  return !direct && !full && !existing;
}
}  // namespace rex::graphics::gta4_metal
