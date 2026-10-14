#pragma once

#include <rex/ui/presentation_clock.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>

namespace rex::ui::metal {

inline uint64_t MetalTimeNanoseconds(double seconds) noexcept {
  const long double value = static_cast<long double>(seconds) * FramePacer::kSecond;
  if (!std::isfinite(seconds) || value <= 0 || value >= static_cast<long double>(UINT64_MAX))
    return 0;
  return static_cast<uint64_t>(value);
}

inline bool IsUsableDisplayOpportunity(uint64_t now, uint64_t target, uint64_t deadline) {
  return now && target > now && target - now <= FramePacer::kSecond &&
      deadline && deadline <= target;
}

// Keep display opportunities alive across ordinary gaps between publications.
// A callback with no invalidation does not draw. Only sustained inactivity
// pauses callbacks; this grace period is not another frame-rate schedule.
// All accesses belong to the UI thread.
class DisplayLinkActivity {
 public:
  static constexpr uint64_t kIdleGraceNs = FramePacer::kSecond / 4;
  void Demand(uint64_t now_ns) { last_demand_ns_ = now_ns; }
  bool ShouldPause(uint64_t now_ns, bool pending) {
    if (pending || !last_demand_ns_ || now_ns < last_demand_ns_) {
      Demand(now_ns);
      return false;
    }
    return now_ns - last_demand_ns_ >= kIdleGraceNs;
  }
 private:
  uint64_t last_demand_ns_ = 0;
};

// One pending wakeup, not a second FPS clock. A stale dispatch event checks
// the current deadline before consuming it; an earlier invalidation can replace
// it. Main-thread owner, independent of any particular dispatch API.
class SoftwarePaintDeadline {
 public:
  bool Request(uint64_t now_ns, uint64_t delay_ns) {
    const uint64_t proposed = FramePacer::Add(now_ns, std::clamp<uint64_t>(delay_ns, 1, FramePacer::kSecond));
    if (due_ns_ && due_ns_ <= proposed) return false;
    due_ns_ = proposed;
    return true;
  }
  uint64_t due() const { return due_ns_; }
  bool Consume(uint64_t now_ns) {
    if (!due_ns_ || now_ns < due_ns_) return false;
    due_ns_ = 0;
    return true;
  }
  void Reset() { due_ns_ = 0; }
 private:
  uint64_t due_ns_ = 0;
};

struct DisplayFeedback {
  uint64_t surface_epoch = 0, generation = 0, publication = 0;
  uint64_t queue_host_ns = 0, target_host_ns = 0, actual_host_ns = 0;
  uint32_t frame = 0, fps = 0;
  enum class Stage { kPresented, kScheduled, kCompleted } stage = Stage::kPresented;
  uint64_t callback_host_ns = 0, deadline_host_ns = 0;
  uint64_t event_host_ns = 0, gpu_start_ns = 0, gpu_end_ns = 0;
  bool gpu_error = false, generated = false;
  uint64_t presentation_id = 0;
};

// Presentation callbacks may outlive a resized surface or the presenter. They
// hold only this bounded mailbox, never a raw window/renderer/frame pointer.
// The UI paint owner drains it and rejects old epochs before using a timestamp.
class DisplayFeedbackInbox {
 public:
  static constexpr size_t kCapacity = 64;
  struct Batch {
    std::array<DisplayFeedback, kCapacity> values{};
    size_t count = 0;
    uint64_t overwritten = 0;
  };
  void Push(DisplayFeedback value) {
    std::lock_guard lock(mutex_);
    if (count_ == kCapacity) {
      read_ = (read_ + 1) % kCapacity;
      --count_;
      ++overwritten_;
    }
    values_[(read_ + count_) % kCapacity] = value;
    ++count_;
  }
  Batch Drain() {
    std::lock_guard lock(mutex_);
    Batch batch;
    batch.count = count_;
    batch.overwritten = overwritten_;
    for (size_t i = 0; i < count_; ++i) batch.values[i] = values_[(read_ + i) % kCapacity];
    read_ = count_ = 0;
    overwritten_ = 0;
    return batch;
  }
 private:
  std::mutex mutex_;
  std::array<DisplayFeedback, kCapacity> values_{};
  size_t read_ = 0, count_ = 0;
  uint64_t overwritten_ = 0;
};

inline bool IsCurrentDisplayReceipt(const DisplayFeedback& feedback,
    uint64_t epoch, uint64_t generation, uint32_t fps, uint64_t now_ns) noexcept {
  return feedback.surface_epoch == epoch && feedback.generation == generation &&
      feedback.fps == fps && now_ns >= feedback.queue_host_ns &&
      now_ns - feedback.queue_host_ns <= 2 * FramePacer::kSecond;
}
inline bool IsCurrentDisplayFeedback(const DisplayFeedback& feedback,
    uint64_t epoch, uint64_t generation, uint32_t fps, uint64_t now_ns) noexcept {
  return IsCurrentDisplayReceipt(feedback, epoch, generation, fps, now_ns) &&
      feedback.actual_host_ns != 0 && feedback.actual_host_ns >= feedback.queue_host_ns &&
      feedback.actual_host_ns <= FramePacer::Add(now_ns, 1'000'000);
}
}  // namespace rex::ui::metal
