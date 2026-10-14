#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rex::graphics::gta4_native {

// Insert-only open-addressing set for small trivially copyable keys, reused
// across frames: Clear() keeps the storage, so steady-state frames do no heap
// allocation. Keys are stored whole and compared with ==, never by hash alone.
template <typename Key, typename Hash>
class NativeFlatSet {
 public:
  // Returns true when the key was not present.
  bool Insert(const Key& key) {
    if ((size_ + 1) * 2 > slots_.size()) Grow();
    const size_t mask = slots_.size() - 1;
    for (size_t i = Hash{}(key) & mask;; i = (i + 1) & mask) {
      Slot& slot = slots_[i];
      if (slot.epoch != epoch_) {
        slot.key = key;
        slot.epoch = epoch_;
        ++size_;
        return true;
      }
      if (slot.key == key) return false;
    }
  }
  void Clear() {
    size_ = 0;
    if (++epoch_ == 0) {  // wrapped: invalidate every slot explicitly
      for (Slot& slot : slots_) slot.epoch = 0;
      epoch_ = 1;
    }
  }
  size_t size() const { return size_; }

 private:
  struct Slot {
    Key key{};
    uint32_t epoch = 0;
  };
  void Grow() {
    std::vector<Slot> old;
    old.swap(slots_);
    slots_.assign(old.empty() ? 1024 : old.size() * 2, Slot{});
    const uint32_t previous_epoch = epoch_;
    size_ = 0;
    epoch_ = 1;
    for (const Slot& slot : old)
      if (slot.epoch == previous_epoch) Insert(slot.key);
  }
  std::vector<Slot> slots_;
  size_t size_ = 0;
  uint32_t epoch_ = 1;
};

inline uint64_t NativeMixHash(uint64_t value) {
  value ^= value >> 33;
  value *= 0xff51afd7ed558ccdull;
  value ^= value >> 33;
  value *= 0xc4ceb9fe1a85ec53ull;
  value ^= value >> 33;
  return value;
}

}  // namespace rex::graphics::gta4_native
