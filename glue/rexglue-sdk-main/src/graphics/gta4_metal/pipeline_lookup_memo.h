#pragma once

#include <array>
#include <cstddef>

namespace rex::graphics::gta4_metal {

// Same exact key as the owning pipeline map, never a hash-only shortcut.
// The owner resets the memo BEFORE erasing any map entry. Rehashing keeps the
// map's element addresses stable. Compilation/readiness stays in the normal
// pipeline path; this memo does not imply that a pipeline has finished building.
template<class Key, class Pipeline, size_t Capacity = 4>
class PipelineLookupMemo {
 public:
  static_assert(Capacity > 0);
  PipelineLookupMemo() = default;
  PipelineLookupMemo(const PipelineLookupMemo&) = delete;
  PipelineLookupMemo& operator=(const PipelineLookupMemo&) = delete;

  Pipeline* Find(const Key& key) const {
    for (size_t age = 0; age < Capacity; ++age) {
      const auto& entry = entries_[(next_ + Capacity - 1 - age) % Capacity];
      if (entry.pipeline && entry.key == key) return entry.pipeline;
    }
    return nullptr;
  }
  void Remember(const Key& key, Pipeline* pipeline) {
    if (!pipeline) return;
    entries_[next_] = {key, pipeline};
    next_ = (next_ + 1) % Capacity;
  }
  void Reset() {
    for (auto& entry : entries_) entry.pipeline = nullptr;
    next_ = 0;
  }
 private:
  struct Entry { Key key{}; Pipeline* pipeline = nullptr; };
  std::array<Entry, Capacity> entries_{};
  size_t next_ = 0;
};

}  // namespace rex::graphics::gta4_metal
