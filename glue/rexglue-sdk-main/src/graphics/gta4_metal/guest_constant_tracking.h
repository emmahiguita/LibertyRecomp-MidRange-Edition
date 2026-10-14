#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace rex::graphics::gta4_metal {
// Dirty words 0/1 describe VS/PS float banks. Observe EVERY draw and clear,
// even if it is culled, empty, or rejected before an upload. Cached-list replay
// supplies full masks; mutable deferred-light effects require full snapshots.
class GuestConstantTracking {
 public:
  void Observe(uint32_t device, uint64_t vertex, uint64_t pixel, bool force) {
    if (device != device_) { Reset(); device_ = device; }
    if (vertex || force) clean_[0] = false;
    if (pixel || force) clean_[1] = false;
  }
  bool Clean(size_t bank) const { return clean_[bank]; }
  void Copied(size_t bank) { clean_[bank] = true; }
  void Reset() { clean_.fill(false); }
 private:
  uint32_t device_ = 0;
  std::array<bool, 2> clean_{};
};
}  // namespace rex::graphics::gta4_metal
