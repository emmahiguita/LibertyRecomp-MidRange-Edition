#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace rex::thread::detail {

// A private wakeup for one wait operation. The generation is sampled BEFORE
// checking object predicates. Notifications between that check and parking
// therefore cannot be lost. No object lock is held during WaitUntil().
class MultiWaitSignal {
 public:
  uint64_t Observe() {
    std::lock_guard lock(mutex_);
    return generation_;
  }
  void Notify() {
    std::lock_guard lock(mutex_);
    ++generation_;
    condition_.notify_one();
  }
  void WaitUntil(uint64_t observed, std::chrono::steady_clock::time_point deadline) {
    std::unique_lock lock(mutex_);
    const auto changed = [&] { return generation_ != observed; };
    if (deadline == std::chrono::steady_clock::time_point::max()) {
      condition_.wait(lock, changed);
    } else {
      condition_.wait_until(lock, deadline, changed);
    }
  }
 private:
  std::mutex mutex_;
  std::condition_variable condition_;
  uint64_t generation_ = 0;
};

// Short-lived intrusive registrations avoid allocation on signal delivery.
// Lock order: object predicate -> source -> private signal. A waiting thread
// never holds its private signal lock when accessing an object or a source.
class MultiWaitSource {
 public:
  class Registration {
   public:
    Registration() = default;
    Registration(const Registration&) = delete;
    Registration& operator=(const Registration&) = delete;
    ~Registration() { Disconnect(); }
    void Connect(MultiWaitSource& source, MultiWaitSignal& signal) {
      Disconnect();
      std::lock_guard lock(source.mutex_);
      source_ = &source;
      signal_ = &signal;
      next_ = source.first_;
      if (next_) next_->previous_ = this;
      source.first_ = this;
    }
    void Disconnect() {
      if (!source_) return;
      std::lock_guard lock(source_->mutex_);
      if (previous_) previous_->next_ = next_;
      else source_->first_ = next_;
      if (next_) next_->previous_ = previous_;
      source_ = nullptr;
      signal_ = nullptr;
      previous_ = next_ = nullptr;
    }
   private:
    friend class MultiWaitSource;
    MultiWaitSource* source_ = nullptr;
    MultiWaitSignal* signal_ = nullptr;
    Registration* previous_ = nullptr;
    Registration* next_ = nullptr;
  };

  void Notify() {
    std::lock_guard lock(mutex_);
    for (auto* entry = first_; entry; entry = entry->next_) {
      entry->signal_->Notify();
    }
  }
 private:
  std::mutex mutex_;
  Registration* first_ = nullptr;
};

inline std::chrono::steady_clock::time_point MultiWaitDeadline(
    std::chrono::milliseconds timeout) {
  const auto now = std::chrono::steady_clock::now();
  const auto maximum = std::chrono::steady_clock::time_point::max();
  if (timeout == std::chrono::milliseconds::max() ||
      timeout >= std::chrono::duration_cast<std::chrono::milliseconds>(maximum - now)) {
    return maximum;
  }
  return timeout <= std::chrono::milliseconds::zero() ? now : now + timeout;
}

}  // namespace rex::thread::detail
