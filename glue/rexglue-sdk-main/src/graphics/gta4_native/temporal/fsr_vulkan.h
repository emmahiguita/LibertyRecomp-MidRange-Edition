#pragma once

#include <memory>
#include <string>

#include "frame_input.h"

namespace rex::ui::vulkan { class VulkanDevice; }

namespace rex::graphics::gta4_native::temporal {

// Checks the logical features required by permutations selected by the pinned
// SDK. A context-creation success alone does not certify enabled shader features.
bool FsrSupportsDevice(const ui::vulkan::VulkanDevice& device,
                       std::string* reason = nullptr, bool frame_generation = false);

// One context per view, recorded and submitted in order on the graphics queue.
// Configure changes and Shutdown require completion of all uses of this object;
// a renderer that retires resources by fences must retire this object with them.
class FsrUpscalerVulkan {
 public:
  FsrUpscalerVulkan();
  ~FsrUpscalerVulkan();
  FsrUpscalerVulkan(const FsrUpscalerVulkan&) = delete;
  FsrUpscalerVulkan& operator=(const FsrUpscalerVulkan&) = delete;

  bool Initialize(VkPhysicalDevice physical_device, VkDevice device,
                  PFN_vkGetDeviceProcAddr get_device_proc_addr);
  bool QueryRenderExtent(Extent output, Quality quality, Extent& render);
  bool QueryJitter(uint64_t scene_index, Extent render, Extent output, float& x, float& y);
  bool Configure(const Configuration& configuration);
  bool Encode(VkCommandBuffer command_buffer, const FrameInput& frame,
              bool sharpening = false, float sharpness = 0.0f);
  // A successful Encode only means commands were recorded. Commit history only
  // after vkQueueSubmit succeeds. Reset the canceled command buffer before
  // OnDiscarded, then Configure after prior GPU uses complete before re-encoding.
  void OnSubmitted();
  void OnDiscarded();
  void Reset();
  void Shutdown();

  bool available() const;
  const std::string& provider_name() const;
  const std::string& last_error() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

enum class FsrTransferFunction : uint8_t { kSrgb, kPq, kScRgb };

struct FsrFrameGenerationInput {
  // Display-sized scene after tone mapping, before HUD. The generated image is
  // also HUD-free; the presenter composites the current display-sized UI onto
  // both generated and real images. Both resources use GENERAL layout.
  Image present_color;
  Image generated_output;
  FsrTransferFunction transfer_function = FsrTransferFunction::kSrgb;
  float minimum_luminance = 0.0f;
  float maximum_luminance = 0.0f;
  float view_space_to_meters = 1.0f;
  float camera_position[3]{};
  float camera_up[3]{};
  float camera_right[3]{};
  float camera_forward[3]{};
};

// Direct FSR interpolation, intentionally independent of the SDK proxy
// swapchain. The caller owns pacing, real/generated presentation, UI composition,
// Vulkan barriers, and lifetime through the last consuming GPU submission.
class FsrFrameGenerationVulkan {
 public:
  FsrFrameGenerationVulkan();
  ~FsrFrameGenerationVulkan();
  FsrFrameGenerationVulkan(const FsrFrameGenerationVulkan&) = delete;
  FsrFrameGenerationVulkan& operator=(const FsrFrameGenerationVulkan&) = delete;

  bool Initialize(VkPhysicalDevice physical_device, VkDevice device,
                  PFN_vkGetDeviceProcAddr get_device_proc_addr);
  bool Configure(const Configuration& configuration, VkFormat present_format);
  // Records both prepare and optical-flow/interpolation in the same command
  // buffer. The first frame after a reset seeds history; present only real color
  // unless has_generated_frame() is true and submission succeeds.
  bool Encode(VkCommandBuffer command_buffer, const FrameInput& frame,
              const FsrFrameGenerationInput& presentation);
  void OnSubmitted();
  void OnDiscarded();
  void Reset();
  void Shutdown();

  bool available() const;
  bool has_generated_frame() const;
  const std::string& provider_name() const;
  const std::string& last_error() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace rex::graphics::gta4_native::temporal
