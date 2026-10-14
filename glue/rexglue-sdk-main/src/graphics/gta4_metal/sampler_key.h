#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace rex::graphics::gta4_metal {
struct SamplerKey {
  std::array<uint32_t, 10> words{};
  bool operator==(const SamplerKey&) const = default;
};
struct SamplerKeyHash {
  size_t operator()(const SamplerKey& key) const noexcept {
    uint64_t hash = 14695981039346656037ull;
    for (uint32_t word : key.words) { hash ^= word; hash *= 1099511628211ull; }
    return size_t(hash);
  }
};
}  // namespace rex::graphics::gta4_metal
