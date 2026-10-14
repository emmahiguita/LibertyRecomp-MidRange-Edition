#pragma once

#include <array>
#include <vector>
#include <string_view>

#include <rex/ui/vulkan/api.h>

#include "postfx_resource_pool.h"
#include "split_postfx_parameters.h"
#include "native_gpu_timing.h"

namespace rex::ui::vulkan {
class VulkanDevice;
}

namespace rex::graphics::gta4_native {

class SplitPostFxPass {
 public:
  bool Record(VkCommandBuffer command_buffer, const ui::vulkan::VulkanDevice* device,
              VkDescriptorPool descriptor_pool, VkPipelineCache pipeline_cache,
              VkImage destination_image, VkImageView destination_view, VkImageView depth_view,
              VkImageView stipple_mask_view, VkImageView half_scene_view, VkFormat color_format, PostFxExtent extent,
              const SplitPostFxParameters& parameters, PostFxResourcePool& resources,
              const NativeGpuTimingSink* timing = nullptr, std::string_view* failure = nullptr);
  void Destroy(const ui::vulkan::VulkanDevice* device);

 private:
  struct Pipeline {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkPipeline pipeline = VK_NULL_HANDLE;
  };

  bool EnsureObjects(const ui::vulkan::VulkanDevice* device);
  VkPipeline GetOrCreatePipeline(const ui::vulkan::VulkanDevice* device,
                                 VkPipelineCache pipeline_cache, VkFormat format);
  bool RecordPass(VkCommandBuffer command_buffer, const ui::vulkan::VulkanDevice* device,
                  VkDescriptorPool descriptor_pool, VkPipeline pipeline,
                  const std::array<VkImageView, 4>& inputs, PostFxResourcePool::Image& destination,
                  uint32_t pass_index, PostFxExtent source_extent, PostFxExtent full_extent,
                  const SplitPostFxParameters& parameters);

  VkSampler sampler_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
  std::vector<Pipeline> pipelines_;
};

}  // namespace rex::graphics::gta4_native
