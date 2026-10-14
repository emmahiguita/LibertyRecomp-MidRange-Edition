// Standalone macOS GPU check for the production profiling-label helpers.
// Exercises attachment clearing and transfer with labels on and off. This is
// not a material-shader test or a gameplay performance benchmark.
#include "graphics/gta4_native/native_profile_labels.h"
#include <array>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <vector>
namespace rex {
void InitLoggingEarly();
}
using namespace rex::graphics::gta4_native;
using namespace rex::ui::vulkan;
static void Check(VkResult result) {
  if (result != VK_SUCCESS)
    throw std::runtime_error("Vulkan failure=" + std::to_string(result));
}
struct Objects {
  const VulkanDevice *device;
  VkImage image{};
  VkImageView view{};
  VkDeviceMemory image_memory{}, buffer_memory{};
  VkBuffer buffer{};
  VkCommandPool pool{};
  VkFence fence{};
  ~Objects() {
    const auto &fn = device->functions();
    const auto d = device->device();
    fn.vkDeviceWaitIdle(d);
    if (fence)
      fn.vkDestroyFence(d, fence, nullptr);
    if (pool)
      fn.vkDestroyCommandPool(d, pool, nullptr);
    if (view)
      fn.vkDestroyImageView(d, view, nullptr);
    if (image)
      fn.vkDestroyImage(d, image, nullptr);
    if (buffer)
      fn.vkDestroyBuffer(d, buffer, nullptr);
    if (image_memory)
      fn.vkFreeMemory(d, image_memory, nullptr);
    if (buffer_memory)
      fn.vkFreeMemory(d, buffer_memory, nullptr);
  }
};
int main(int argc, char **argv) {
  try {
    if (argc != 2)
      return 2;
    rex::InitLoggingEarly();
    auto instance = VulkanInstance::Create(false, false);
    if (!instance)
      return 3;
    std::vector<VkPhysicalDevice> physical;
    instance->EnumeratePhysicalDevices(physical);
    if (physical.empty())
      return 4;
    auto device = VulkanDevice::CreateIfSupported(instance.get(), physical[0],
                                                  false, false, false, true);
    if (!device)
      return 5;
    const auto d = device->device();
    const auto &fn = device->functions();
    Objects obj{device.get()};
    VkPhysicalDeviceMemoryProperties memory{};
    instance->functions().vkGetPhysicalDeviceMemoryProperties(physical[0],
                                                              &memory);
    auto allocate = [&](VkMemoryRequirements req, VkMemoryPropertyFlags flags,
                        VkDeviceMemory &dst) {
      uint32_t type = UINT32_MAX;
      for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
        if ((req.memoryTypeBits & (1u << i)) &&
            (memory.memoryTypes[i].propertyFlags & flags) == flags) {
          type = i;
          break;
        }
      if (type == UINT32_MAX)
        throw std::runtime_error("No memory type");
      VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      info.allocationSize = req.size;
      info.memoryTypeIndex = type;
      Check(fn.vkAllocateMemory(d, &info, nullptr, &dst));
    };
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = VK_FORMAT_R8G8B8A8_UNORM;
    image.extent = {8, 8, 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    Check(fn.vkCreateImage(d, &image, nullptr, &obj.image));
    VkMemoryRequirements req{};
    fn.vkGetImageMemoryRequirements(d, obj.image, &req);
    allocate(req, 0, obj.image_memory);
    Check(fn.vkBindImageMemory(d, obj.image, obj.image_memory, 0));
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = obj.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = image.format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    Check(fn.vkCreateImageView(d, &view, nullptr, &obj.view));
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer.size = 256;
    buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    Check(fn.vkCreateBuffer(d, &buffer, nullptr, &obj.buffer));
    fn.vkGetBufferMemoryRequirements(d, obj.buffer, &req);
    allocate(req,
             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
             obj.buffer_memory);
    Check(fn.vkBindBufferMemory(d, obj.buffer, obj.buffer_memory, 0));
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.queueFamilyIndex = device->queue_family_graphics_compute();
    Check(fn.vkCreateCommandPool(d, &pool, nullptr, &obj.pool));
    VkCommandBufferAllocateInfo alloc{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = obj.pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer command{};
    Check(fn.vkAllocateCommandBuffers(d, &alloc, &command));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(fn.vkBeginCommandBuffer(command, &begin));
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = obj.image;
    barrier.subresourceRange = view.subresourceRange;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    fn.vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                            nullptr, 0, nullptr, 1, &barrier);
    NativeGpuScopeSummary summary;
    VkRenderingAttachmentInfo attachment{
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    attachment.imageView = obj.view;
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.clearValue.color = {{1, 0, 1, 1}};
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = {8, 8};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &attachment;
    gpu_labels::BeginSceneScope(device.get(), command, rendering, &summary, 1,
                                7);
    fn.vkCmdBeginRendering(command, &rendering);
    fn.vkCmdEndRendering(command);
    gpu_labels::End(device.get(), command);
    gpu_labels::EndSceneScope(device.get(), command, summary);
    if (summary.active())
      throw std::runtime_error("Unclosed scope");
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    fn.vkCmdPipelineBarrier(
        command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {8, 8, 1};
    gpu_labels::Transfer(device.get(), command, "GTA4/fixture/readback", [&] {
      fn.vkCmdCopyImageToBuffer(command, obj.image,
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                obj.buffer, 1, &region);
    });
    VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    fn.vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, nullptr,
                            0, nullptr);
    Check(fn.vkEndCommandBuffer(command));
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    Check(fn.vkCreateFence(d, &fence, nullptr, &obj.fence));
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    {
      auto queue =
          device->AcquireQueue(device->queue_family_graphics_compute(), 0);
      Check(fn.vkQueueSubmit(queue.queue(), 1, &submit, obj.fence));
    }
    Check(fn.vkWaitForFences(d, 1, &obj.fence, VK_TRUE, UINT64_C(5000000000)));
    void *mapped{};
    Check(fn.vkMapMemory(d, obj.buffer_memory, 0, VK_WHOLE_SIZE, 0, &mapped));
    std::array<unsigned char, 256> pixels{};
    std::memcpy(pixels.data(), mapped, pixels.size());
    fn.vkUnmapMemory(d, obj.buffer_memory);
    const std::array<unsigned char, 4> expected{255, 0, 255, 255};
    for (size_t i = 0; i < pixels.size(); ++i)
      if (pixels[i] != expected[i % expected.size()])
        throw std::runtime_error("Pixel mismatch");
    std::ofstream output(argv[1], std::ios::binary);
    output.write(reinterpret_cast<const char *>(pixels.data()), pixels.size());
    if (!output)
      throw std::runtime_error("Output write failed");
    printf("labels=%d available=%d bytes=%zu pixels_match=true gpu=%s\n",
           gpu_labels::Enabled(), gpu_labels::Available(device.get(), command),
           pixels.size(), device->properties().deviceName);
    return 0;
  } catch (const std::exception &e) {
    fprintf(stderr, "%s\n", e.what());
    return 1;
  }
}
