#pragma once

#include <cstdint>
#include <mutex>
#include <utility>

namespace rex::ui::metal {
// At most one acquisition is outstanding, even across surface resets. Reset
// cancels its result, not the system call. The caller never waits on that call.
template<class Drawable> class DrawableRequest {
 public:
  struct Ticket { uint64_t epoch = 0, id = 0; explicit operator bool() const { return epoch && id; } };
  Ticket Begin() {
    std::lock_guard lock(mutex_);
    if (stopped_ || pending_ || ready_ || next_id_ == UINT64_MAX) return {};
    pending_ = true;
    return {epoch_, ++next_id_};
  }
  bool Complete(Ticket ticket, Drawable drawable) {
    std::lock_guard lock(mutex_);
    if (!pending_ || !ticket || ticket.id != next_id_) return false;
    pending_ = false;
    if (stopped_ || ticket.epoch != epoch_) return false;
    drawable_ = std::move(drawable);
    ready_ = bool(drawable_);
    return true;
  }
  Drawable Take() {
    std::lock_guard lock(mutex_);
    ready_ = false;
    return std::exchange(drawable_, Drawable{});
  }
  void Reset(bool stop = false) {
    std::lock_guard lock(mutex_);
    ++epoch_;
    if (!epoch_) { stopped_ = true; epoch_ = 1; }
    stopped_ |= stop;
    drawable_ = {}; ready_ = false;
  }
  bool pending() const { std::lock_guard lock(mutex_); return pending_; }
 private:
  mutable std::mutex mutex_;
  uint64_t epoch_ = 1, next_id_ = 0;
  bool pending_ = false, ready_ = false, stopped_ = false;
  Drawable drawable_{};
};
}  // namespace rex::ui::metal
