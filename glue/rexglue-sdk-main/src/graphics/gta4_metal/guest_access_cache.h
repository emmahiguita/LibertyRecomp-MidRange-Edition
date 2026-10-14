#pragma once
#include <array>
#include <cstdint>
#include <rex/memory/utils.h>

namespace rex::graphics::gta4_metal {
// Per-thread page-range permissions, not guest contents or buffer generations.
// Callers check heap bounds before lookup. Misses always query the real heap;
// entries are published only if the epoch stayed stable across that query.
class GuestAccessCache {
 public:
  bool Find(const void* heap, uint64_t epoch, uint32_t first_page, uint32_t last_page,
            memory::PageAccess& access) const {
    if (!epoch) return false;
    const auto slot = Slot(epoch, first_page, last_page);
    const auto* entry = &entries_[slot];
    if (!Matches(*entry, heap, epoch, first_page, last_page)) {
      entry = &entries_[CollisionSlot(epoch, first_page, last_page, slot)];
      if (!Matches(*entry, heap, epoch, first_page, last_page)) return false;
    }
    access = entry->access;
    return true;
  }
  void Remember(const void* heap, uint64_t before, uint64_t after, uint32_t first_page, uint32_t last_page,
                memory::PageAccess access) {
    if (!before || before != after) return;
    auto slot = Slot(before, first_page, last_page);
    const auto& entry = entries_[slot];
    if (entry.heap == heap && entry.epoch == before &&
        (entry.first != first_page || entry.last != last_page)) {
      slot = CollisionSlot(before, first_page, last_page, slot);
    }
    entries_[slot] = {heap, before, first_page, last_page, access};
  }
 private:
  static size_t Slot(uint64_t epoch, uint32_t first, uint32_t last) {
    return (epoch ^ (uint64_t(first) * 0x9E3779B1u) ^ (uint64_t(last) << 7)) & 255;
  }
  static size_t CollisionSlot(uint64_t epoch, uint32_t first, uint32_t last, size_t primary) {
    // Keep the cheap primary lookup for adjacent buffers. Its low-bit hash
    // aliases ranges separated by 256 pages, so a second, bounded probe mixes
    // the entire range with the SplitMix64 finalizer. No scan or allocation.
    uint64_t hash = (uint64_t(first) << 32) | last;
    hash = (hash ^ (hash >> 30)) * 0xBF58476D1CE4E5B9ull;
    hash = (hash ^ (hash >> 27)) * 0x94D049BB133111EBull;
    const size_t slot = (hash ^ (hash >> 31) ^ epoch) & 255;
    return slot == primary ? slot ^ 128 : slot;
  }
  struct Entry {
    const void* heap = nullptr;
    uint64_t epoch = 0;
    uint32_t first = 0, last = 0;
    memory::PageAccess access = memory::PageAccess::kNoAccess;
  };
  static bool Matches(const Entry& entry, const void* heap, uint64_t epoch, uint32_t first, uint32_t last) {
    return entry.heap == heap && entry.epoch == epoch && entry.first == first && entry.last == last;
  }
  std::array<Entry, 256> entries_{};
};
}  // namespace rex::graphics::gta4_metal
