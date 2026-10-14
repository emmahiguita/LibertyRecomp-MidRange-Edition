#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <mutex>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace rex::ui::vulkan {

// One ordering timeline per shared mailbox image. A read is an access too:
// publishing producer completion alone does not prevent read/write reuse races.
struct ImageAccessTicket {
  VkSemaphore semaphore = VK_NULL_HANDLE;
  uint64_t wait_value = 0;
  uint64_t signal_value = 0;
  bool valid() const {
    return !semaphore || (signal_value && signal_value > wait_value);
  }
};

class ImageAccessTimeline {
 public:
  class Lease {
   public:
    Lease() = default;
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;
    Lease(Lease&& other) noexcept
        : owner_(std::exchange(other.owner_, nullptr)), lock_(std::move(other.lock_)),
          ticket_(other.ticket_) {}
    Lease& operator=(Lease&& other) noexcept {
      if (this != &other) {
        lock_ = std::move(other.lock_);
        owner_ = std::exchange(other.owner_, nullptr);
        ticket_ = other.ticket_;
      }
      return *this;
    }
    const ImageAccessTicket& ticket() const { return ticket_; }
    // Called exactly at successful queue acceptance, never on an abandoned
    // recording. The next lease must not wait for a signal that was not submitted.
    bool Commit() {
      if (!owner_) return !ticket_.semaphore;
      if (!ticket_.valid() || !lock_.owns_lock()) return false;
      owner_->submitted_value_ = ticket_.signal_value;
      owner_ = nullptr;
      lock_.unlock();
      return true;
    }
   private:
    friend class ImageAccessTimeline;
    explicit Lease(ImageAccessTimeline& owner) {
      if (!owner.semaphore_) return;
      lock_ = std::unique_lock(owner.mutex_);
      owner_ = &owner;
      ticket_ = {owner.semaphore_, owner.submitted_value_,
                 owner.submitted_value_ == std::numeric_limits<uint64_t>::max()
                     ? 0 : owner.submitted_value_ + 1};
    }
    ImageAccessTimeline* owner_ = nullptr;
    std::unique_lock<std::mutex> lock_;
    ImageAccessTicket ticket_{};
  };

  // Initialization precedes publication; semaphore ownership stays with the image.
  void Initialize(VkSemaphore semaphore) { semaphore_ = semaphore; }
  Lease Acquire() { return Lease(*this); }
  uint64_t submitted_value() const {
    std::lock_guard lock(mutex_);
    return submitted_value_;
  }
 private:
  VkSemaphore semaphore_ = VK_NULL_HANDLE;
  mutable std::mutex mutex_;
  uint64_t submitted_value_ = 0;
};

// Append a mailbox timeline to an existing binary-only submission (including
// WSI acquire/render-finished semaphores). Storage lasts through vkQueueSubmit.
// No allocations and no change to the pNext chain when separation is disabled.
class ImageAccessSubmit {
 public:
  bool Attach(VkSubmitInfo& submit, const ImageAccessTicket& ticket) {
    if (!ticket.valid()) return false;
    if (!ticket.semaphore) return true;
    if (submit.waitSemaphoreCount >= kCapacity || submit.signalSemaphoreCount >= kCapacity ||
        (submit.waitSemaphoreCount && (!submit.pWaitSemaphores || !submit.pWaitDstStageMask)) ||
        (submit.signalSemaphoreCount && !submit.pSignalSemaphores)) return false;
    for (auto* next = static_cast<const VkBaseInStructure*>(submit.pNext); next;
         next = next->pNext) {
      if (next->sType == VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO) return false;
    }
    for (uint32_t i = 0; i < submit.waitSemaphoreCount; ++i) {
      waits_[i] = submit.pWaitSemaphores[i];
      stages_[i] = submit.pWaitDstStageMask[i];
      wait_values_[i] = 0;  // ignored for the existing binary semaphores
    }
    for (uint32_t i = 0; i < submit.signalSemaphoreCount; ++i) {
      signals_[i] = submit.pSignalSemaphores[i];
      signal_values_[i] = 0;
    }
    waits_[submit.waitSemaphoreCount] = ticket.semaphore;
    stages_[submit.waitSemaphoreCount] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    wait_values_[submit.waitSemaphoreCount] = ticket.wait_value;
    signals_[submit.signalSemaphoreCount] = ticket.semaphore;
    signal_values_[submit.signalSemaphoreCount] = ticket.signal_value;
    timeline_.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timeline_.pNext = submit.pNext;
    timeline_.waitSemaphoreValueCount = ++submit.waitSemaphoreCount;
    timeline_.pWaitSemaphoreValues = wait_values_.data();
    timeline_.signalSemaphoreValueCount = ++submit.signalSemaphoreCount;
    timeline_.pSignalSemaphoreValues = signal_values_.data();
    submit.pNext = &timeline_;
    submit.pWaitSemaphores = waits_.data();
    submit.pWaitDstStageMask = stages_.data();
    submit.pSignalSemaphores = signals_.data();
    return true;
  }
 private:
  static constexpr uint32_t kCapacity = 4;
  VkTimelineSemaphoreSubmitInfo timeline_{};
  std::array<VkSemaphore, kCapacity> waits_{}, signals_{};
  std::array<VkPipelineStageFlags, kCapacity> stages_{};
  std::array<uint64_t, kCapacity> wait_values_{}, signal_values_{};
};

}  // namespace rex::ui::vulkan
