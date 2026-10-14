#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace rex::graphics::gta4_native {
// Borrowed descriptor handles only. The frame pool owns their lifetime.
// No entry survives pool reset or a switch of the recording context.
template <typename Descriptor>
class NativeResolveDescriptorCache {
 public:
  struct Key {
    uint64_t view = 0, surface_lifetime = 0, layout = 0;
    uint32_t image_layout = 0;
    bool operator==(const Key&) const = default;
  };
  struct Statistics { uint64_t hits = 0, misses = 0, inserts = 0; };
  void BeginScope(uint64_t command_buffer, uint64_t pool) {
    if (command_buffer_ != command_buffer || pool_ != pool) {
      Reset(); command_buffer_ = command_buffer; pool_ = pool;
    }
  }
  void Reset() { entries_.clear(); command_buffer_ = pool_ = 0; }
  Descriptor Find(const Key& key) {
    if (Valid(key)) {
      const auto found = entries_.find(key);
      if (found != entries_.end()) { ++statistics_.hits; return found->second; }
    }
    ++statistics_.misses; return {};
  }
  void Insert(const Key& key, Descriptor descriptor) {
    if (!descriptor || !Valid(key)) return;
    if (entries_.size() >= kCapacity) entries_.clear();
    if (entries_.emplace(key, descriptor).second) ++statistics_.inserts;
  }
  const Statistics& statistics() const { return statistics_; }
  size_t size() const { return entries_.size(); }
  static constexpr size_t kCapacity = 256;
 private:
  bool Valid(const Key& key) const {
    return command_buffer_ && pool_ && key.view && key.surface_lifetime && key.layout;
  }
  struct Hash {
    size_t operator()(const Key& key) const {
      size_t h = std::hash<uint64_t>{}(key.view);
      const auto mix = [&](uint64_t v) {
        h ^= std::hash<uint64_t>{}(v) + size_t(0x9e3779b9u) + (h << 6) + (h >> 2);
      };
      mix(key.surface_lifetime); mix(key.layout); mix(key.image_layout); return h;
    }
  };
  std::unordered_map<Key, Descriptor, Hash> entries_;
  uint64_t command_buffer_ = 0, pool_ = 0;
  Statistics statistics_;
};
}  // namespace rex::graphics::gta4_native
