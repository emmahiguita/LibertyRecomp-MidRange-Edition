#pragma once

#include <cstdint>
#include <memory>
#include <rex/ui/vulkan/api.h>

namespace rex::ui::vulkan {

// Both images are immutable final FP16 presentation images. Interpolation is
// performed on the scene without HUD, then the current HUD is composited onto
// both. Host UI drawers run later at the swapchain resolution.
struct GeneratedFrame {
  VkImage generated = VK_NULL_HANDLE;
  VkImageView generated_view = VK_NULL_HANDLE;
  VkImage real = VK_NULL_HANDLE;
  VkImageView real_view = VK_NULL_HANDLE;
  VkExtent2D extent{};
  std::shared_ptr<void> lease;
  uint64_t epoch = 0;
  uint64_t interval_ns = 0;

  bool valid(VkImage mailbox_image, VkExtent2D mailbox_extent) const {
    return generated && generated_view && real && real_view && lease &&
           generated != real && generated_view != real_view && generated != mailbox_image &&
           real != mailbox_image && extent.width && extent.height &&
           extent.width == mailbox_extent.width && extent.height == mailbox_extent.height &&
           interval_ns && interval_ns <= 100'000'000;
  }
};

// Queue acceptance is the only operation that advances a pair. Failed or
// retried paints keep selecting the same stage, and a newer scene cannot
// replace an already presented generated frame's retained real partner.
class GeneratedFrameDelivery {
 public:
  enum class Stage { kUnpaired, kGenerated, kReal };

  Stage Select(uint64_t publication, bool candidate_ready, uint64_t surface_epoch) const {
    if (pending_publication_ && surface_epoch == pending_surface_epoch_) return Stage::kReal;
    return candidate_ready && publication > last_generated_publication_ ? Stage::kGenerated
                                                                       : Stage::kUnpaired;
  }
  void Presented(Stage stage, uint64_t publication, uint64_t surface_epoch) {
    if (stage == Stage::kGenerated) {
      last_generated_publication_ = publication;
      pending_publication_ = publication;
      pending_surface_epoch_ = surface_epoch;
    } else if (stage == Stage::kReal && publication == pending_publication_ &&
               surface_epoch == pending_surface_epoch_) {
      ResetPending();
    }
  }
  bool has_pending(uint64_t surface_epoch) const {
    return pending_publication_ && pending_surface_epoch_ == surface_epoch;
  }
  void ResetPending() { pending_publication_ = pending_surface_epoch_ = 0; }

 private:
  uint64_t last_generated_publication_ = 0;
  uint64_t pending_publication_ = 0;
  uint64_t pending_surface_epoch_ = 0;
};

}  // namespace rex::ui::vulkan
