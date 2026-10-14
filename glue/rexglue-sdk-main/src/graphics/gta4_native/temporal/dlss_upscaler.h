#pragma once

#include <memory>
#include <string>

#include "frame_input.h"

namespace rex::ui::vulkan {
class VulkanDevice;
}

namespace rex::graphics::gta4_native::temporal {

struct DlssOptimalSettings {
  Extent render_extent;
  Extent minimum_extent;
  Extent maximum_extent;
};

bool ValidDlssSuperResolutionInput(const FrameInput& frame, bool hdr);

class DlssUpscaler {
 public:
  DlssUpscaler();
  ~DlssUpscaler();
  DlssUpscaler(const DlssUpscaler&) = delete;
  DlssUpscaler& operator=(const DlssUpscaler&) = delete;

  bool Initialize(const ui::vulkan::VulkanDevice& device);
  bool QueryOptimalSettings(Extent output, Quality quality, DlssOptimalSettings& settings);

  // Creates the SDK feature in a private command buffer and completes that
  // initialization before returning. Reconfiguration retires prior GPU work.
  bool Configure(const Configuration& configuration);
  bool Encode(VkCommandBuffer command_buffer, const FrameInput& frame);

  // Exactly one notification follows an encoded frame. All evaluations must be
  // submitted in order on the device's native-offscreen graphics queue. The
  // caller must reset an unsubmitted command buffer BEFORE OnDiscarded.
  void OnSubmitted(uint64_t sequence);
  void OnDiscarded();
  void Shutdown();

  bool available() const;
  const std::string& status() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace rex::graphics::gta4_native::temporal
