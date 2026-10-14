#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <new>
#include <utility>

namespace rex::system {

// Separate from the ordered kernel callback dispatcher. Each file identity is
// assigned a stable FIFO lane, including handles referring to the same XFile.
// The owner supplies bound XHostThreads; tests can use ordinary host threads.
class OrderedIoQueue final {
 public:
  static constexpr size_t kMaxLanes = 4;
  enum class Admission { kAccepted, kClosed, kFull, kNoMemory };
  struct Statistics {
    uint64_t accepted = 0;
    uint64_t completed = 0;
    uint64_t rejected = 0;
    uint64_t maximum_queue_wait_ns = 0;
    uint64_t maximum_service_ns = 0;
    size_t queued = 0;
    size_t active = 0;
    size_t high_water = 0;
  };

  explicit OrderedIoQueue(size_t lanes = kMaxLanes, size_t capacity = 4096)
      : lane_count_(std::clamp(lanes, size_t{1}, kMaxLanes)),
        capacity_(std::max(capacity, size_t{1})) {}
  OrderedIoQueue(const OrderedIoQueue&) = delete;
  OrderedIoQueue& operator=(const OrderedIoQueue&) = delete;

  size_t lane_count() const noexcept { return lane_count_; }
  size_t LaneFor(uintptr_t identity) const noexcept {
    // XFile allocations are aligned; do not select a lane from alignment bits.
    const uintptr_t mixed = (identity >> 4) ^ (identity >> 13) ^ (identity >> 23);
    return mixed % lane_count_;
  }

  Admission Admit(uintptr_t identity, std::function<void()> work,
                  std::function<void()> publish_pending = {}) {
    std::unique_lock lock(mutex_);
    if (!accepting_ || !work) {
      ++stats_.rejected;
      return Admission::kClosed;
    }
    if (stats_.queued + stats_.active >= capacity_) {
      ++stats_.rejected;
      return Admission::kFull;
    }
    const size_t lane = LaneFor(identity);
    try {
      // Allocate queue storage before moving the callback. On allocation
      // failure, retained kernel objects are released after mutex_ unlocks.
      lanes_[lane].queue.push_back({{}, Clock::now()});
    } catch (const std::bad_alloc&) {
      ++stats_.rejected;
      return Admission::kNoMemory;
    }
    lanes_[lane].queue.back().work = std::move(work);
    // Admission is durable before IOSB/event changes, and the worker cannot
    // observe the job until those changes finish. This callback must not throw:
    // recovering after partially publishing PENDING could strand a guest wait.
    if (publish_pending) {
      try {
        publish_pending();
      } catch (...) {
        std::terminate();
      }
    }
    ++stats_.accepted;
    ++stats_.queued;
    stats_.high_water = std::max(stats_.high_water, stats_.queued + stats_.active);
    lock.unlock();
    lanes_[lane].ready.notify_one();
    return Admission::kAccepted;
  }

  void RunLane(size_t lane_index) noexcept {
    if (lane_index >= lane_count_) std::terminate();
    auto& lane = lanes_[lane_index];
    for (;;) {
      Job job;
      {
        std::unique_lock lock(mutex_);
        lane.ready.wait(lock, [&] { return !lane.queue.empty() || !accepting_; });
        if (lane.queue.empty()) return;
        job = std::move(lane.queue.front());
        lane.queue.pop_front();
        --stats_.queued;
        ++stats_.active;
        stats_.maximum_queue_wait_ns =
            std::max(stats_.maximum_queue_wait_ns, Nanoseconds(Clock::now() - job.enqueued));
      }
      const auto start = Clock::now();
      try {
        job.work();
      } catch (...) {
        // Same fail-fast contract as the kernel's original host dispatcher.
        std::terminate();
      }
      // Release retained files, events, buffers and threads before advertising
      // idle. Their destructors can take kernel locks; never run under mutex_.
      job.work = {};
      {
        std::lock_guard lock(mutex_);
        --stats_.active;
        ++stats_.completed;
        stats_.maximum_service_ns =
            std::max(stats_.maximum_service_ns, Nanoseconds(Clock::now() - start));
        if (stats_.queued == 0 && stats_.active == 0) idle_.notify_all();
      }
    }
  }

  // Close prevents new work but never discards accepted work. Join the supplied
  // workers before destroying this object. Drain also includes capture release.
  void Close() noexcept {
    {
      std::lock_guard lock(mutex_);
      accepting_ = false;
    }
    for (size_t lane = 0; lane < lane_count_; ++lane) lanes_[lane].ready.notify_all();
  }
  void Drain() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [&] { return stats_.queued == 0 && stats_.active == 0; });
  }
  Statistics statistics() const {
    std::lock_guard lock(mutex_);
    return stats_;
  }

 private:
  using Clock = std::chrono::steady_clock;
  static uint64_t Nanoseconds(Clock::duration duration) noexcept {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
  }
  struct Job {
    std::function<void()> work;
    Clock::time_point enqueued;
  };
  struct Lane {
    std::deque<Job> queue;
    std::condition_variable ready;
  };
  const size_t lane_count_;
  const size_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable idle_;
  std::array<Lane, kMaxLanes> lanes_;
  bool accepting_ = true;
  Statistics stats_;
};

}  // namespace rex::system
