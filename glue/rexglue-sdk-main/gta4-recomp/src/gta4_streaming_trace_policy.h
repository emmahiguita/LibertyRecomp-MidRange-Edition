#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gta4::streaming {

// Diagnostic-only bounded cache. Recording every entity on every frame can
// drown request/state events before a useful gameplay trace is collected.
// A generation or raw classification/alpha change is always emitted for a
// matching entry. Collisions only add events; they never hide another key.
class ClassificationTraceCache final {
 public:
  static constexpr size_t kCapacity = 32768;
  static constexpr size_t kWays = 4;
  struct Observation {
    uint64_t generation = 0;
    uint64_t time = 0;
    uint32_t entity = 0;
    uint32_t view = 0;
    uint32_t model = 0;
    uint32_t entry = 0xFFFF;
    uint8_t result = 0;
    uint8_t alpha = 0;
  };

  void Reset() noexcept { slots_.fill({}); }

  bool ShouldEmit(const Observation& observation) noexcept {
    const size_t bucket = Bucket(observation) * kWays;
    Slot* victim = &slots_[bucket];
    for (size_t index = 0; index < kWays; ++index) {
      Slot& slot = slots_[bucket + index];
      if (slot.valid && SameKey(slot.value, observation)) {
        const bool changed = slot.value.generation != observation.generation ||
                             slot.value.result != observation.result ||
                             slot.value.alpha != observation.alpha;
        slot.value = observation;
        return changed;
      }
      if (!slot.valid || (victim->valid && slot.value.time < victim->value.time)) victim = &slot;
    }
    *victim = Slot{observation, true};
    return true;
  }

 private:
  struct Slot { Observation value{}; bool valid = false; };
  static bool SameKey(const Observation& a, const Observation& b) noexcept {
    return a.entity == b.entity && a.view == b.view && a.model == b.model && a.entry == b.entry;
  }
  static size_t Bucket(const Observation& value) noexcept {
    uint64_t key = (uint64_t{value.entity} << 32) | value.view;
    key ^= uint64_t{value.model} * 0x9E3779B97F4A7C15ULL;
    key ^= key >> 30;
    key *= 0xBF58476D1CE4E5B9ULL;
    key ^= key >> 27;
    return static_cast<size_t>(key) & (kCapacity / kWays - 1);
  }
  std::array<Slot, kCapacity> slots_{};
};

}  // namespace gta4::streaming
