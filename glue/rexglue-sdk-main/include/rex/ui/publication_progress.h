#pragma once
#include <atomic>
#include <cstdint>

namespace rex::ui {
// Notifications are wakeups, not images. A late notification for an image
// already submitted cannot manufacture another presentation. The producer
// publishes only after the mailbox release, and acceptance names the image
// actually submitted (not whichever image is newest at that instant).
class PublicationProgress {
 public:
  void Publish(uint64_t serial) { Advance(published_, serial); }
  void Accept(uint64_t serial) { Advance(accepted_, serial); }
  bool pending() const {
    const auto published = published_.load(std::memory_order_acquire);
    return published > accepted_.load(std::memory_order_acquire);
  }
 private:
  static void Advance(std::atomic<uint64_t>& destination, uint64_t value) {
    auto old = destination.load(std::memory_order_relaxed);
    for (; old < value && !destination.compare_exchange_weak(old, value,
        std::memory_order_release, std::memory_order_relaxed);) {}
  }
  std::atomic<uint64_t> published_{0}, accepted_{0};
};
}  // namespace rex::ui
