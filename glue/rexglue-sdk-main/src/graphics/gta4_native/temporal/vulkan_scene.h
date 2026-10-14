#pragma once

#include <array>
#include <memory>
#include <string>

#include "frame_input.h"

namespace rex::ui::vulkan { class VulkanDevice; }

namespace rex::graphics::gta4_native::temporal {

// Resources are shared by consecutive submissions on the native graphics queue.
// The owner retires every submission before changing the extent or destroying
// this object. Descriptor sets are allocated per recording and retired with it.
class VulkanScene {
 public:
  struct OwnedImage {
    Image image;
    VkDeviceMemory memory = VK_NULL_HANDLE;
  };
  struct DepthSource {
    Image image;
    VkImageAspectFlags barrier_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
  };
  struct Camera {
    std::array<float, 16> background_reprojection{};
    std::array<float, 2> jitter{}, half_pixel{};
    bool reversed = false, background_history = false;
  };

  bool Initialize(const ui::vulkan::VulkanDevice*);
  bool Matches(Configuration config) const;
  bool Configure(Configuration config);
  bool Begin(VkCommandBuffer);
  bool Prepare(VkCommandBuffer, VkDescriptorPool, const DepthSource&, const Camera&);
  // Record after Prepare and all scene draws. The caller must submit and wait
  // for this command buffer's fence before reading; no previous-frame result
  // can certify the current frame. Each result can be consumed only once.
  bool RecordAudit(VkCommandBuffer, VkDescriptorPool);
  bool ReadAuditAfterCompletion();
  bool SpatialFallback(VkCommandBuffer, const Image& source);
  void OutputReadable(VkCommandBuffer);
  void AttachmentsWritable(VkCommandBuffer);
  void Shutdown();
  const std::string& error() const { return error_; }
  const Configuration& configuration() const { return configuration_; }
  Image& motion() { return motion_.image; }
  Image& reactive() { return reactive_.image; }
  Image& previous_depth() { return previous_depth_.image; }
  Image& depth() { return depth_.image; }
  Image& output() { return output_.image; }
  Image& display() { return display_.image; }
  Image& filtered() { return filtered_.image; }
  static void Barrier(const ui::vulkan::VulkanDevice*, VkCommandBuffer, Image&,
                      VkImageLayout next, VkImageAspectFlags aspect = 0);

 private:
  bool Create(OwnedImage&, VkFormat, Extent, VkImageUsageFlags);
  void Destroy(OwnedImage&);
  bool BindScene(VkCommandBuffer, VkDescriptorPool, const Image&);
  const ui::vulkan::VulkanDevice* device_ = nullptr;
  Configuration configuration_{};
  OwnedImage motion_, reactive_, previous_depth_, depth_, output_, display_, filtered_;
  VkSampler sampler_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  VkBuffer audit_buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory audit_memory_ = VK_NULL_HANDLE;
  void* audit_mapping_ = nullptr;
  bool prepared_ = false, audit_recorded_ = false;
  std::string error_;
};
}  // namespace rex::graphics::gta4_native::temporal
