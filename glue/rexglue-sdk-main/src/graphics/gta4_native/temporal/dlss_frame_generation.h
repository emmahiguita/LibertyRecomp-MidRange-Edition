#pragma once

#include <array>
#include <memory>
#include <string>

#include "frame_input.h"

namespace rex::ui::vulkan { class VulkanDevice; }

namespace rex::graphics::gta4_native::temporal {

struct DlssFrameGenerationInput {
  // Display-sized, postprocessed scene with no HUD. The presenter composites
  // the same current UI on generated and real outputs after interpolation.
  Image present_color;
  Image generated_output;
  // Optional copy of present_color retained by NGX. If absent, the caller must
  // retain present_color unchanged until the real frame has been presented.
  Image retained_real_output;

  // Unjittered row-major matrices, for row vectors post-multiplied by matrices,
  // exactly as the direct NGX FG guide specifies. No inferred camera motion.
  std::array<float, 16> camera_view_to_clip{};
  std::array<float, 16> clip_to_camera_view{};
  std::array<float, 16> clip_to_previous_clip{};
  std::array<float, 16> previous_clip_to_clip{};
  std::array<float, 3> camera_position{};
  std::array<float, 3> camera_up{};
  std::array<float, 3> camera_right{};
  std::array<float, 3> camera_forward{};
  // The actual clip-space jitter applied by the projection. Explicit because
  // Vulkan viewport orientation determines the render-pixel -> clip Y sign.
  std::array<float, 2> projection_jitter{};
  float camera_aspect_ratio = 0.0f;
  bool camera_valid = false;
};

bool ValidDlssFrameGenerationInput(const FrameInput& frame, const DlssFrameGenerationInput& input,
                                  VkFormat format);

class DlssFrameGenerator {
 public:
  DlssFrameGenerator();
  ~DlssFrameGenerator();
  DlssFrameGenerator(const DlssFrameGenerator&) = delete;
  DlssFrameGenerator& operator=(const DlssFrameGenerator&) = delete;

  bool Initialize(const ui::vulkan::VulkanDevice& device);
  bool Configure(const Configuration& configuration, VkFormat present_format, bool hdr_present);
  bool Encode(VkCommandBuffer command_buffer, const FrameInput& frame,
              const DlssFrameGenerationInput& input);
  void OnSubmitted(uint64_t sequence);
  // Reset the abandoned command buffer before this notification.
  void OnDiscarded();
  void Shutdown();
  bool available() const;
  // A successful Encode alone is insufficient to present: also require
  // successful submission and completion, then pace generated before real.
  // The first/reset frame is a history seed and never a generated presentation.
  bool has_generated_frame() const;
  const std::string& status() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace rex::graphics::gta4_native::temporal
