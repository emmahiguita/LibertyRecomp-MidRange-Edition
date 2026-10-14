#include "vulkan_scene.h"

#include <cstddef>
#include <cstring>
#include <limits>
#include <rex/ui/vulkan/device.h>
#include <rex/ui/vulkan/instance.h>
#include <rex/ui/vulkan/util.h>

#include "vulkan_scene_spv.h"

namespace rex::graphics::gta4_native::temporal {
namespace {
struct Parameters {
  std::array<float, 16> reprojection;
  std::array<float, 2> jitter, half_pixel;
  std::array<uint32_t, 2> extent;
  uint32_t reversed, history;
};
static_assert(sizeof(Parameters) == 96 && offsetof(Parameters, extent) == 80);
constexpr VkImageUsageFlags kCommon = VK_IMAGE_USAGE_SAMPLED_BIT |
    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
struct AuditCounts {
  uint32_t covered, invalid_motion, invalid_depth, invalid_previous_depth;
};
static_assert(sizeof(AuditCounts) == 16 && offsetof(AuditCounts, invalid_previous_depth) == 12);
}

void VulkanScene::Barrier(const ui::vulkan::VulkanDevice* device, VkCommandBuffer cb,
                          Image& image, VkImageLayout next, VkImageAspectFlags aspect) {
  VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.srcAccessMask = image.layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 :
      VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  barrier.oldLayout = image.layout;
  barrier.newLayout = next;
  barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image.image;
  barrier.subresourceRange = {aspect ? aspect : image.aspect, 0, 1, 0, 1};
  device->functions().vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
  image.layout = next;
}

bool VulkanScene::Initialize(const ui::vulkan::VulkanDevice* device) {
  if (pipeline_) return device_ == device;
  if (!device) return false;
  device_ = device;
  const auto& fn = device->functions();
  VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
  sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  if (fn.vkCreateSampler(device->device(), &sampler, nullptr, &sampler_) != VK_SUCCESS) {
    error_ = "temporal depth sampler creation failed"; Shutdown(); return false;
  }
  if (!ui::vulkan::util::CreateDedicatedAllocationBuffer(device, sizeof(AuditCounts),
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      ui::vulkan::util::MemoryPurpose::kReadback, audit_buffer_, audit_memory_) ||
      fn.vkMapMemory(device->device(), audit_memory_, 0, VK_WHOLE_SIZE, 0, &audit_mapping_) != VK_SUCCESS) {
    error_ = "temporal motion coverage readback allocation failed"; Shutdown(); return false;
  }
  std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
  for (uint32_t i = 0; i < bindings.size(); ++i) {
    bindings[i].binding = i;
    bindings[i].descriptorType = i ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[i].descriptorCount = 1;
    bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  }
  bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  bindings[0].pImmutableSamplers = &sampler_;
  VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  set.bindingCount = uint32_t(bindings.size()); set.pBindings = bindings.data();
  if (fn.vkCreateDescriptorSetLayout(device->device(), &set, nullptr, &set_layout_) != VK_SUCCESS) {
    error_ = "temporal image descriptor layout creation failed"; Shutdown(); return false;
  }
  VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Parameters)};
  VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout.setLayoutCount = 1; layout.pSetLayouts = &set_layout_;
  layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
  if (fn.vkCreatePipelineLayout(device->device(), &layout, nullptr, &pipeline_layout_) != VK_SUCCESS) {
    error_ = "temporal compute pipeline layout creation failed"; Shutdown(); return false;
  }
  const VkShaderModule shader = ui::vulkan::util::CreateShaderModule(device,
      kVulkanSceneSpirv, sizeof(kVulkanSceneSpirv));
  if (!shader) { error_ = "temporal depth/background shader creation failed"; Shutdown(); return false; }
  VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pipeline.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  pipeline.stage.module = shader; pipeline.stage.pName = "main";
  pipeline.layout = pipeline_layout_;
  const VkResult result = fn.vkCreateComputePipelines(device->device(), VK_NULL_HANDLE, 1,
      &pipeline, nullptr, &pipeline_);
  fn.vkDestroyShaderModule(device->device(), shader, nullptr);
  if (result != VK_SUCCESS) { error_ = "temporal depth/background pipeline creation failed"; Shutdown(); return false; }
  return true;
}

bool VulkanScene::Create(OwnedImage& out, VkFormat format, Extent extent, VkImageUsageFlags usage) {
  VkFormatProperties properties{};
  device_->vulkan_instance()->functions().vkGetPhysicalDeviceFormatProperties(
      device_->physical_device(), format, &properties);
  VkFormatFeatureFlags required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
      VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
  if (usage & VK_IMAGE_USAGE_STORAGE_BIT) required |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
  if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) required |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
  if ((properties.optimalTilingFeatures & required) != required) {
    error_ = "device does not support the temporal image format/usage"; return false;
  }
  VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  info.imageType = VK_IMAGE_TYPE_2D; info.format = format;
  info.extent = {extent.width, extent.height, 1}; info.mipLevels = info.arrayLayers = 1;
  info.samples = VK_SAMPLE_COUNT_1_BIT; info.tiling = VK_IMAGE_TILING_OPTIMAL;
  info.usage = usage | kCommon; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (!ui::vulkan::util::CreateDedicatedAllocationImage(device_, info,
      ui::vulkan::util::MemoryPurpose::kDeviceLocal, out.image.image, out.memory)) {
    error_ = "temporal image allocation failed"; return false;
  }
  VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  view.image = out.image.image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = format;
  view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  if (device_->functions().vkCreateImageView(device_->device(), &view, nullptr, &out.image.view) != VK_SUCCESS) {
    error_ = "temporal image view creation failed"; Destroy(out); return false;
  }
  out.image.format = format; out.image.extent = extent;
  out.image.usage = info.usage; out.image.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
  return true;
}

void VulkanScene::Destroy(OwnedImage& image) {
  if (!device_) return;
  const auto& fn = device_->functions();
  if (image.image.view) fn.vkDestroyImageView(device_->device(), image.image.view, nullptr);
  if (image.image.image) fn.vkDestroyImage(device_->device(), image.image.image, nullptr);
  if (image.memory) fn.vkFreeMemory(device_->device(), image.memory, nullptr);
  image = {};
}

bool VulkanScene::Matches(Configuration config) const {
  return output_.image.image && config == configuration_;
}
bool VulkanScene::Configure(Configuration config) {
  if (!device_ || !pipeline_ || !config.render_extent || !config.output_extent) return false;
  if (Matches(config)) return true;
  prepared_ = audit_recorded_ = false;
  for (auto* image : {&motion_, &reactive_, &previous_depth_, &depth_, &output_, &display_, &filtered_}) Destroy(*image);
  configuration_ = config;
  const auto attachment = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
  if (!Create(motion_, VK_FORMAT_R16G16_SFLOAT, config.render_extent, attachment) ||
      !Create(reactive_, VK_FORMAT_R8_UNORM, config.render_extent, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ||
      !Create(previous_depth_, VK_FORMAT_R32_SFLOAT, config.render_extent, attachment) ||
      !Create(depth_, VK_FORMAT_R32_SFLOAT, config.render_extent, VK_IMAGE_USAGE_STORAGE_BIT) ||
      !Create(output_, VK_FORMAT_R16G16B16A16_SFLOAT, config.output_extent, attachment) ||
      !Create(display_, VK_FORMAT_R16G16B16A16_SFLOAT, config.output_extent, attachment) ||
      !Create(filtered_, VK_FORMAT_R16G16B16A16_SFLOAT, config.render_extent, attachment)) {
    for (auto* image : {&motion_, &reactive_, &previous_depth_, &depth_, &output_, &display_, &filtered_}) Destroy(*image);
    return false;
  }
  return true;
}

bool VulkanScene::Begin(VkCommandBuffer cb) {
  prepared_ = audit_recorded_ = false;
  if (!output_.image.image) return false;
  const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  for (auto* image : {&motion_, &reactive_, &previous_depth_}) {
    Barrier(device_, cb, image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearColorValue clear{};
    if (image == &previous_depth_) clear.float32[0] = -1.0f;
    device_->functions().vkCmdClearColorImage(cb, image->image.image,
        image->image.layout, &clear, 1, &range);
  }
  AttachmentsWritable(cb);
  return true;
}
void VulkanScene::AttachmentsWritable(VkCommandBuffer cb) {
  for (auto* image : {&motion_, &reactive_, &previous_depth_})
    Barrier(device_, cb, image->image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

bool VulkanScene::BindScene(VkCommandBuffer cb, VkDescriptorPool pool, const Image& input) {
  VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocation.descriptorPool = pool; allocation.descriptorSetCount = 1; allocation.pSetLayouts = &set_layout_;
  VkDescriptorSet set = VK_NULL_HANDLE;
  if (device_->functions().vkAllocateDescriptorSets(device_->device(), &allocation, &set) != VK_SUCCESS) {
    error_ = "temporal scene descriptor allocation failed"; return false;
  }
  std::array<VkDescriptorImageInfo, 4> images{{
    {sampler_, input.view, input.layout},
    {VK_NULL_HANDLE, depth_.image.view, VK_IMAGE_LAYOUT_GENERAL},
    {VK_NULL_HANDLE, motion_.image.view, VK_IMAGE_LAYOUT_GENERAL},
    {VK_NULL_HANDLE, previous_depth_.image.view, VK_IMAGE_LAYOUT_GENERAL}}};
  std::array<VkWriteDescriptorSet, 5> writes{};
  for (uint32_t i = 0; i < images.size(); ++i) {
    writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[i].dstSet = set; writes[i].dstBinding = i; writes[i].descriptorCount = 1;
    writes[i].descriptorType = i ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[i].pImageInfo = &images[i];
  }
  const VkDescriptorBufferInfo audit{audit_buffer_, 0, sizeof(AuditCounts)};
  writes[4] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  writes[4].dstSet = set; writes[4].dstBinding = 4; writes[4].descriptorCount = 1;
  writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[4].pBufferInfo = &audit;
  const auto& fn = device_->functions();
  fn.vkUpdateDescriptorSets(device_->device(), uint32_t(writes.size()), writes.data(), 0, nullptr);
  fn.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
  fn.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_, 0, 1, &set, 0, nullptr);
  return true;
}

bool VulkanScene::Prepare(VkCommandBuffer cb, VkDescriptorPool pool,
                          const DepthSource& source, const Camera& camera) {
  prepared_ = audit_recorded_ = false;
  if (!source.image || source.image.extent != configuration_.render_extent || !pool) return false;
  Image input = source.image;
  const auto original_layout = input.layout;
  Barrier(device_, cb, input, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, source.barrier_aspect);
  for (auto* image : {&depth_, &motion_, &previous_depth_, &reactive_, &output_})
    Barrier(device_, cb, image->image, VK_IMAGE_LAYOUT_GENERAL);
  if (!BindScene(cb, pool, input)) {
    Barrier(device_, cb, input, original_layout, source.barrier_aspect); return false;
  }
  const auto& fn = device_->functions();
  Parameters parameters{camera.background_reprojection, camera.jitter, camera.half_pixel,
      {configuration_.render_extent.width, configuration_.render_extent.height},
      uint32_t(camera.reversed), uint32_t(camera.background_history)};
  fn.vkCmdPushConstants(cb, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(parameters), &parameters);
  fn.vkCmdDispatch(cb, (parameters.extent[0] + 7) / 8, (parameters.extent[1] + 7) / 8, 1);
  Barrier(device_, cb, input, original_layout, source.barrier_aspect);
  for (auto* image : {&depth_, &motion_, &previous_depth_})
    Barrier(device_, cb, image->image, VK_IMAGE_LAYOUT_GENERAL);
  prepared_ = true;
  return true;
}

bool VulkanScene::RecordAudit(VkCommandBuffer cb, VkDescriptorPool pool) {
  audit_recorded_ = false;
  const auto extent = configuration_.render_extent;
  if (!prepared_ || !cb || !pool || !audit_mapping_ ||
      uint64_t(extent.width) * extent.height > std::numeric_limits<uint32_t>::max()) return false;
  for (auto* image : {&depth_, &motion_, &previous_depth_})
    Barrier(device_, cb, image->image, VK_IMAGE_LAYOUT_GENERAL);
  if (!BindScene(cb, pool, depth_.image)) return false;
  const auto& fn = device_->functions();
  VkBufferMemoryBarrier buffer{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  buffer.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  buffer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  buffer.srcQueueFamilyIndex = buffer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  buffer.buffer = audit_buffer_; buffer.size = sizeof(AuditCounts);
  fn.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
      0, 0, nullptr, 1, &buffer, 0, nullptr);
  fn.vkCmdFillBuffer(cb, audit_buffer_, 0, sizeof(AuditCounts), 0);
  buffer.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  buffer.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
  fn.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      0, 0, nullptr, 1, &buffer, 0, nullptr);
  Parameters parameters{}; parameters.extent = {extent.width, extent.height}; parameters.history = 2;
  fn.vkCmdPushConstants(cb, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(parameters), &parameters);
  fn.vkCmdDispatch(cb, (extent.width + 7) / 8, (extent.height + 7) / 8, 1);
  buffer.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; buffer.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  fn.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
      0, 0, nullptr, 1, &buffer, 0, nullptr);
  audit_recorded_ = true;
  return true;
}

bool VulkanScene::ReadAuditAfterCompletion() {
  if (!audit_recorded_ || !audit_mapping_) return false;
  audit_recorded_ = false;
  // The entire allocation is mapped. Whole-size invalidation is valid on both
  // coherent and noncoherent host-visible memory without alignment rounding.
  VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
  range.memory = audit_memory_; range.size = VK_WHOLE_SIZE;
  if (device_->functions().vkInvalidateMappedMemoryRanges(device_->device(), 1, &range) != VK_SUCCESS) {
    error_ = "temporal motion coverage readback invalidation failed"; return false;
  }
  AuditCounts counts{}; std::memcpy(&counts, audit_mapping_, sizeof(counts));
  const uint64_t expected = uint64_t(configuration_.render_extent.width) * configuration_.render_extent.height;
  if (counts.covered != expected || counts.invalid_motion || counts.invalid_depth || counts.invalid_previous_depth) {
    error_ = "visible temporal motion is incomplete: pixels=" + std::to_string(counts.covered) +
        " motion=" + std::to_string(counts.invalid_motion) + " depth=" + std::to_string(counts.invalid_depth) +
        " correspondence=" + std::to_string(counts.invalid_previous_depth);
    return false;
  }
  return true;
}

bool VulkanScene::SpatialFallback(VkCommandBuffer cb, const Image& source) {
  if (!source || !output_.image.image || source.image == output_.image.image) return false;
  VkFormatProperties properties{};
  device_->vulkan_instance()->functions().vkGetPhysicalDeviceFormatProperties(
      device_->physical_device(), source.format, &properties);
  if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
      !(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) return false;
  device_->vulkan_instance()->functions().vkGetPhysicalDeviceFormatProperties(
      device_->physical_device(), output_.image.format, &properties);
  if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT)) return false;
  Image input = source;
  Barrier(device_, cb, input, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  Barrier(device_, cb, output_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  VkImageBlit blit{};
  blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  blit.srcOffsets[1] = {int32_t(source.extent.width), int32_t(source.extent.height), 1};
  blit.dstOffsets[1] = {int32_t(output_.image.extent.width), int32_t(output_.image.extent.height), 1};
  device_->functions().vkCmdBlitImage(cb, source.image, input.layout, output_.image.image,
      output_.image.layout, 1, &blit, VK_FILTER_LINEAR);
  Barrier(device_, cb, input, source.layout);
  OutputReadable(cb);
  return true;
}
void VulkanScene::OutputReadable(VkCommandBuffer cb) {
  Barrier(device_, cb, output_.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
void VulkanScene::Shutdown() {
  if (!device_) return;
  for (auto* image : {&motion_, &reactive_, &previous_depth_, &depth_, &output_, &display_, &filtered_}) Destroy(*image);
  const auto& fn = device_->functions(); const auto vk = device_->device();
  if (audit_mapping_) fn.vkUnmapMemory(vk, audit_memory_);
  if (audit_buffer_) fn.vkDestroyBuffer(vk, audit_buffer_, nullptr);
  if (audit_memory_) fn.vkFreeMemory(vk, audit_memory_, nullptr);
  if (pipeline_) fn.vkDestroyPipeline(vk, pipeline_, nullptr);
  if (pipeline_layout_) fn.vkDestroyPipelineLayout(vk, pipeline_layout_, nullptr);
  if (set_layout_) fn.vkDestroyDescriptorSetLayout(vk, set_layout_, nullptr);
  if (sampler_) fn.vkDestroySampler(vk, sampler_, nullptr);
  pipeline_ = VK_NULL_HANDLE; pipeline_layout_ = VK_NULL_HANDLE;
  set_layout_ = VK_NULL_HANDLE; sampler_ = VK_NULL_HANDLE; device_ = nullptr;
  audit_buffer_ = VK_NULL_HANDLE; audit_memory_ = VK_NULL_HANDLE; audit_mapping_ = nullptr;
  prepared_ = audit_recorded_ = false;
}
}  // namespace rex::graphics::gta4_native::temporal
