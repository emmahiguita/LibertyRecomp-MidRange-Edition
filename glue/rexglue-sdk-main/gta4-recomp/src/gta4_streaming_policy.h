#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>

namespace gta4::streaming {

inline constexpr uint32_t kEntryStride = 24;
inline constexpr uint32_t kInvalidEntry = 0xFFFF;
inline constexpr uint32_t kStateShift = 30;
inline constexpr uint32_t kSizeMask = 0x3FFFFFFF;
inline constexpr uint64_t kNanosecondsPerSecond = 1000000000;

// Never reinterpret a zero configuration field. The original setter still
// queries its allocator and enforces the actual backing capacity.
inline uint32_t RequestedBudget(uint32_t original, double scale) noexcept {
  if (original == 0 || !std::isfinite(scale) || scale <= 1.0) return original;
  const double result = static_cast<double>(original) * std::min(scale, 4.0);
  return static_cast<uint32_t>(
      std::min(result, static_cast<double>(std::numeric_limits<uint32_t>::max())));
}

// stream.ini reserves memory for unmanaged resources. A capacity clamp alone
// must not spend that reserve. Never lower the pre-existing effective limit;
// only the additional budget is eligible for the host headroom adjustment.
inline uint32_t BudgetWithHeadroom(uint32_t original, uint32_t allocator_clamped,
                                   uint32_t reserve_bytes) noexcept {
  const uint32_t original_floor = std::min(original, allocator_clamped);
  const uint32_t with_reserve = allocator_clamped > reserve_bytes
                                   ? allocator_clamped - reserve_bytes : 0;
  return std::max(original_floor, with_reserve);
}

inline uint32_t EntryIndex(uint32_t table, uint32_t address) noexcept {
  if (!table || address < table) return kInvalidEntry;
  const uint32_t offset = address - table;
  if (offset % kEntryStride != 0) return kInvalidEntry;
  const uint32_t index = offset / kEntryStride;
  return index < kInvalidEntry ? index : kInvalidEntry;
}

struct Vec3 { double x = 0, y = 0, z = 0; };
inline bool IsFinite(Vec3 v) noexcept {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline double Length(Vec3 v) noexcept { return std::hypot(v.x, v.y, v.z); }

// No wall-clock/frame-count assumptions and no displacement-per-render-frame
// tuning. A cut, teleport or long pause invalidates prediction immediately.
class MotionTracker {
 public:
  void Update(uint32_t view, uint16_t frame, uint64_t now, Vec3 position) noexcept {
    if (initialized_ && view == view_ && frame == frame_) return;
    if (!IsFinite(position)) { Reset(); return; }
    const bool continuous = initialized_ && view == view_ && now > time_;
    const double elapsed = continuous ? static_cast<double>(now - time_) / kNanosecondsPerSecond : 0;
    const Vec3 delta{position.x - position_.x, position.y - position_.y, position.z - position_.z};
    if (!continuous || elapsed > 0.5 || elapsed < 0.001 || Length(delta) > 100.0) {
      velocity_ = {};
    } else {
      const Vec3 raw{delta.x / elapsed, delta.y / elapsed, delta.z / elapsed};
      if (Length(raw) > 150.0) {
        velocity_ = {};
      } else {
        const double blend = -std::expm1(-elapsed / 0.1);
        velocity_.x += (raw.x - velocity_.x) * blend;
        velocity_.y += (raw.y - velocity_.y) * blend;
        velocity_.z += (raw.z - velocity_.z) * blend;
      }
    }
    initialized_ = true;
    view_ = view;
    frame_ = frame;
    time_ = now;
    position_ = position;
  }
  void Reset() noexcept { initialized_ = false; velocity_ = {}; }
  double ClosingSpeed(Vec3 target) const noexcept {
    if (!initialized_ || !IsFinite(target)) return 0;
    const Vec3 delta{target.x - position_.x, target.y - position_.y, target.z - position_.z};
    const double length = Length(delta);
    if (length < 0.001) return 0;
    return std::clamp((delta.x * velocity_.x + delta.y * velocity_.y + delta.z * velocity_.z) /
                          length, 0.0, 150.0);
  }
  double speed() const noexcept { return Length(velocity_); }
 private:
  bool initialized_ = false;
  uint32_t view_ = 0;
  uint16_t frame_ = 0;
  uint64_t time_ = 0;
  Vec3 position_, velocity_;
};

inline float PreloadMargin(float retail_margin, double minimum_margin, double maximum_margin,
                           double closing_speed, double readiness_seconds) noexcept {
  if (!std::isfinite(retail_margin) || retail_margin < 0 || retail_margin >= 500) return retail_margin;
  if (!std::isfinite(minimum_margin) || !std::isfinite(maximum_margin) ||
      !std::isfinite(closing_speed) || !std::isfinite(readiness_seconds)) return retail_margin;
  const double minimum = std::clamp(minimum_margin, static_cast<double>(retail_margin), 500.0);
  const double maximum = std::clamp(maximum_margin, minimum, 500.0);
  return static_cast<float>(std::clamp(minimum + std::clamp(closing_speed, 0.0, 150.0) *
                                                std::clamp(readiness_seconds, 0.0, 2.0),
                                       minimum, maximum));
}

inline uint64_t SaturatingAdd(uint64_t a, uint64_t b) noexcept {
  return a > UINT64_MAX - b ? UINT64_MAX : a + b;
}
inline uint64_t ReadinessDeadline(uint64_t now, double distance, double draw_distance,
                                  double closing_speed, bool urgent) noexcept {
  if (urgent || !std::isfinite(distance) || !std::isfinite(draw_distance) ||
      distance <= draw_distance) return now;
  double seconds = 0.75;
  if (std::isfinite(closing_speed) && closing_speed > 0.001) {
    seconds = std::clamp((distance - draw_distance) / closing_speed, 0.0, 0.75);
  }
  return SaturatingAdd(now, static_cast<uint64_t>(seconds * kNanosecondsPerSecond));
}

// Side data only: no extra guest references, pointers, states or dependency
// counters are created. Entries are reset on release and table replacement.
// Atomic ranks keep the pending-list walk allocation- and mutex-free.
class DeadlineTable {
 public:
  void Reset() noexcept {
    for (auto& value : deadlines_) value.store(0, std::memory_order_relaxed);
  }
  void Release(uint32_t index) noexcept {
    if (index < kInvalidEntry) deadlines_[index].store(0, std::memory_order_relaxed);
  }
  void Request(uint32_t index, uint64_t deadline) noexcept {
    if (index >= kInvalidEntry) return;
    deadline = std::max(deadline, uint64_t{1});
    auto& target = deadlines_[index];
    uint64_t prior = target.load(std::memory_order_relaxed);
    for (;;) {
      if (prior && prior <= deadline) return;
      if (target.compare_exchange_weak(prior, deadline, std::memory_order_relaxed)) return;
    }
  }
  uint64_t Rank(uint32_t index) const noexcept {
    if (index >= kInvalidEntry) return UINT64_MAX;
    const uint64_t value = deadlines_[index].load(std::memory_order_relaxed);
    return value ? value : UINT64_MAX;
  }
 private:
  std::array<std::atomic<uint64_t>, kInvalidEntry> deadlines_{};
};

}  // namespace gta4::streaming
