#pragma once

#include <array>
#include <memory>
#include <string>

#include <rex/ui/vulkan/presenter.h>

#include "frame_input.h"

namespace rex::graphics::gta4_native::temporal {

enum class FrameGenerationProvider { kFsr, kDlss };

struct FrameGenerationCamera {
  // Actual unjittered camera transforms, row-major / row-vector postmultiply.
  std::array<float, 16> view_to_clip{}, clip_to_view{}, clip_to_previous{}, previous_to_clip{};
  std::array<float, 3> position{}, up{}, right{}, forward{};
  std::array<float, 2> projection_jitter{};
  float aspect_ratio = 0.0f;
  float view_space_to_meters = 1.0f;
  float minimum_luminance = 0.0f;
  float maximum_luminance = 0.0f;
  bool valid = false;
};

struct FrameGenerationResources;

struct FrameGenerationOutput {
  // Successful Encode leaves both in GENERAL. Replay the same current game HUD
  // into both, preserving their contents, then call FinishForPresentation.
  Image generated, real;
  // Distinct destinations for the renderer's final color/EDR conversion. These
  // start in GENERAL too. Both must be written before FinishForPresentation.
  Image generated_present, real_present;
  uint64_t sequence = 0, epoch = 0, interval_ns = 0;
  bool has_generated_frame = false;
  // Also retain this lease through the renderer's submission completion. A
  // false Encode can have recorded commands; a non-null lease still needs a
  // submission/discard notification and GPU lifetime even on that failure path.
  std::shared_ptr<void> lease() const { return resources_; }

 private:
  friend class VulkanFrameGeneration;
  std::shared_ptr<FrameGenerationResources> resources_;
};

class VulkanFrameGeneration {
 public:
  VulkanFrameGeneration();
  ~VulkanFrameGeneration();
  VulkanFrameGeneration(const VulkanFrameGeneration&) = delete;
  VulkanFrameGeneration& operator=(const VulkanFrameGeneration&) = delete;

  // Queries the selected device through an independent SDK instance, preserving
  // any live interpolation history. Cache the result per device lifetime; this
  // may create and destroy a small FSR context and is not a per-frame operation.
  static bool SupportsProvider(const ui::vulkan::VulkanDevice& device,
                               FrameGenerationProvider provider,
                               std::string* reason = nullptr);
  bool Initialize(const ui::vulkan::VulkanDevice& device);
  // Infrequent changes retire prior device work before recreating the provider.
  // hdr_scene describes interpolation input, independently of the later output
  // conversion. A tone-mapped guest sRGB scene sets it false even for EDR output.
  bool Configure(FrameGenerationProvider provider, const Configuration& configuration,
                 bool hdr_scene);
  // Caller barriers all inputs to GENERAL before entry. present_color is final,
  // display-sized FP16 scene without HUD with SAMPLED | TRANSFER_SRC usage.
  // Commands use the same native-offscreen graphics queue as the scene/SR.
  bool Encode(VkCommandBuffer commands, const FrameInput& frame, const Image& present_color,
              const FrameGenerationCamera& camera, const ui::vulkan::VulkanPresenter& presenter,
              FrameGenerationOutput& output);
  // Caller reports actual current layouts of generated_present/real_present
  // after final color conversion. Establishes visibility for the presenter.
  bool FinishForPresentation(VkCommandBuffer commands, FrameGenerationOutput& output);
  void OnSubmitted(uint64_t sequence);
  // Reset the canceled command buffer before calling this method.
  void OnDiscarded();
  // Only succeeds for a finished, nonreset pair whose submission was accepted.
  bool Publish(ui::vulkan::VulkanPresenter::VulkanGuestOutputRefreshContext& context,
               const FrameGenerationOutput& output) const;
  void Reset();
  void Shutdown();
  bool available() const;
  const std::string& status() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace rex::graphics::gta4_native::temporal
