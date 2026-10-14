#pragma once
// Remaps the recompiled shaders' descriptor heaps onto the merged draw layout.
//
// XenosRecomp declares every heap as its own set at binding 0:
//   sets 0-3 sampled-image arrays, set 4 samplers, set 5 the storage buffer.
// Turnip exposes only four bound sets on Adreno 6xx (one more is reserved for
// dynamic descriptors), so the native layout packs the five texture heaps into
// set 0 and moves the storage buffer to set 1, leaving set 2 for the uniform
// constant banks. Within set 0 the samplers come first (binding 0) and the
// image heaps follow (bindings 1-4): ir3 addresses bindless descriptors with
// 16-bit indices relative to the set, so every heap must start low.
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace rex::graphics::gta4_native {

constexpr uint32_t kNativeMergedTextureSet = 0;
constexpr uint32_t kNativeMergedStorageSet = 1;
constexpr uint32_t kNativeLegacyTextureSetCount = 5;
constexpr uint32_t kNativeLegacyStorageSet = 5;
constexpr uint32_t kNativeLegacySamplerSet = 4;

// Binding in the merged texture set for a legacy heap (0-3 images, 4 samplers).
constexpr uint32_t NativeMergedTextureBinding(uint32_t legacy_set) {
  return legacy_set == kNativeLegacySamplerSet ? 0 : legacy_set + 1;
}

// Returns the number of remapped variables. Modules with a set outside the
// legacy range are left untouched (and 0 is returned for them).
inline uint32_t RemapSpirvDrawDescriptorSets(std::vector<uint32_t>& words) {
  constexpr uint16_t kOpDecorate = 71;
  constexpr uint32_t kDecorationBinding = 33, kDecorationDescriptorSet = 34;
  if (words.size() < 5 || words[0] != 0x07230203u) return 0;
  std::unordered_map<uint32_t, uint32_t> sets;  // variable id -> legacy set
  for (size_t i = 5; i < words.size();) {
    const uint32_t count = words[i] >> 16;
    if (!count || i + count > words.size()) return 0;
    if (uint16_t(words[i] & 0xFFFF) == kOpDecorate && count >= 4 &&
        words[i + 2] == kDecorationDescriptorSet) {
      if (words[i + 3] > kNativeLegacyStorageSet) return 0;
      sets[words[i + 1]] = words[i + 3];
    }
    i += count;
  }
  if (sets.empty()) return 0;
  for (size_t i = 5; i < words.size();) {
    const uint32_t count = words[i] >> 16;
    if (uint16_t(words[i] & 0xFFFF) == kOpDecorate && count >= 4) {
      const auto found = sets.find(words[i + 1]);
      if (found != sets.end()) {
        const bool storage = found->second == kNativeLegacyStorageSet;
        if (words[i + 2] == kDecorationDescriptorSet) {
          words[i + 3] = storage ? kNativeMergedStorageSet : kNativeMergedTextureSet;
        } else if (words[i + 2] == kDecorationBinding) {
          words[i + 3] = storage ? 0 : NativeMergedTextureBinding(found->second);
        }
      }
    }
    i += count;
  }
  return uint32_t(sets.size());
}

}  // namespace rex::graphics::gta4_native
