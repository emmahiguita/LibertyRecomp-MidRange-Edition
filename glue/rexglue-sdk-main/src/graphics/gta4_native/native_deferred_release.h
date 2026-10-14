#pragma once
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace rex::graphics::gta4_native {

// Drops batches of shared_ptr owners on a background thread. Used for frame
// state the render worker no longer reads (its release cost - atomic
// decrements and frees - is pure overhead on the frame's critical path).
template <typename T>
class NativeDeferredReleaser {
 public:
  using Batch = std::vector<std::shared_ptr<const T>>;
  NativeDeferredReleaser() : thread_([this] { Run(); }) {}
  ~NativeDeferredReleaser() {
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
    }
    condition_.notify_one();
    thread_.join();
  }
  NativeDeferredReleaser(const NativeDeferredReleaser&) = delete;
  NativeDeferredReleaser& operator=(const NativeDeferredReleaser&) = delete;
  void Release(Batch&& batch) {
    if (batch.empty()) return;
    {
      std::lock_guard lock(mutex_);
      pending_.push_back(std::move(batch));
    }
    condition_.notify_one();
  }

 private:
  void Run() {
    std::unique_lock lock(mutex_);
    for (;;) {
      condition_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
      if (pending_.empty()) return;
      Batch batch = std::move(pending_.front());
      pending_.pop_front();
      lock.unlock();
      batch.clear();
      lock.lock();
    }
  }
  std::mutex mutex_;
  std::condition_variable condition_;
  std::deque<Batch> pending_;
  bool stopping_ = false;
  std::thread thread_;
};

}  // namespace rex::graphics::gta4_native
