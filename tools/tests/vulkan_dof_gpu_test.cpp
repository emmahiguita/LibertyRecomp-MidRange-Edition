// Execute the production DoF SPIR-V on deterministic FusionFix-reference
// fixtures. No game process, assets, shaders or renderer settings are modified.
#include "graphics/gta4_native/split_postfx_parameters.h"
#include "graphics/gta4_native/split_postfx_ps.h"
#include "graphics/shaders/vulkan_spirv/fullscreen_cw_vs.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

using namespace rex::graphics::gta4_native;
namespace {
void Check(bool condition, const std::string &why) {
  if (!condition)
    throw std::runtime_error(why);
}
void V(VkResult result) {
  Check(result == VK_SUCCESS, "Vulkan result " + std::to_string(result));
}
std::vector<uint8_t> Read(const std::string &path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  Check(bool(f), "open " + path);
  auto n = f.tellg();
  Check(n >= 0 && n < 64 * 1024 * 1024, "file size");
  std::vector<uint8_t> b(size_t(n), uint8_t{});
  f.seekg(0);
  f.read((char *)b.data(), n);
  Check(bool(f), "read " + path);
  return b;
}
std::vector<uint32_t> Spirv(const std::string &path) {
  auto b = Read(path);
  Check(b.size() % 4 == 0, "SPIR-V length");
  std::vector<uint32_t> w(b.size() / 4);
  std::memcpy(w.data(), b.data(), b.size());
  return w;
}
struct Buffer {
  VkBuffer handle{};
  VkDeviceMemory memory{};
  void *mapping{};
  VkDeviceAddress address{};
};
struct Image {
  VkImage handle{};
  VkDeviceMemory memory{};
  VkImageView view{};
};
} // namespace
int main(int argc, char **argv) {
  try {
    Check(argc == 3 || argc == 4, "usage: vulkan-dof-test fixture-directory "
                                  "output-directory [reference.spv]");
    std::filesystem::path output(argv[2]);
    std::filesystem::create_directories(output);
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_3;
    app.pApplicationName = "Liberty material-shader verification";
    VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ii.pApplicationInfo = &app;
    VkInstance instance;
    V(vkCreateInstance(&ii, nullptr, &instance));
    uint32_t n = 0;
    V(vkEnumeratePhysicalDevices(instance, &n, nullptr));
    Check(n > 0, "no GPU");
    std::vector<VkPhysicalDevice> physicals(n);
    V(vkEnumeratePhysicalDevices(instance, &n, physicals.data()));
    auto physical = physicals[0];
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical, &props);
    std::cout << "GPU " << props.deviceName << '\n';
    VkPhysicalDeviceVulkan13Features enable{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    enable.dynamicRendering = VK_TRUE;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, nullptr);
    std::vector<VkQueueFamilyProperties> qp(n);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, qp.data());
    uint32_t family = 0;
    for (; family < n; ++family)
      if (qp[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        break;
    Check(family < n, "graphics queue");
    float priority = 1;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = family;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    V(vkEnumerateDeviceExtensionProperties(physical, nullptr, &n, nullptr));
    std::vector<VkExtensionProperties> ex(n);
    V(vkEnumerateDeviceExtensionProperties(physical, nullptr, &n, ex.data()));
    std::vector<const char *> extensions;
    for (auto &e : ex)
      if (!std::strcmp(e.extensionName, "VK_KHR_portability_subset"))
        extensions.push_back("VK_KHR_portability_subset");
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.pNext = &enable;
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = uint32_t(extensions.size());
    di.ppEnabledExtensionNames = extensions.data();
    VkDevice device;
    V(vkCreateDevice(physical, &di, nullptr, &device));
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);
    VkPhysicalDeviceMemoryProperties mem{};
    vkGetPhysicalDeviceMemoryProperties(physical, &mem);
    auto memory_type = [&](uint32_t bits, uint32_t required) {
      for (uint32_t i = 0; i < mem.memoryTypeCount; ++i)
        if ((bits & (1u << i)) &&
            (mem.memoryTypes[i].propertyFlags & required) == required)
          return i;
      throw std::runtime_error("memory type");
    };
    std::vector<Buffer> buffers;
    auto buffer = [&](VkDeviceSize size, VkBufferUsageFlags usage,
                      bool address) {
      Buffer b;
      VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
      ci.size = size;
      ci.usage = usage;
      V(vkCreateBuffer(device, &ci, nullptr, &b.handle));
      VkMemoryRequirements req;
      vkGetBufferMemoryRequirements(device, b.handle, &req);
      VkMemoryAllocateFlagsInfo flags{
          VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
      flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
      VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      ai.pNext = address ? &flags : nullptr;
      ai.allocationSize = req.size;
      ai.memoryTypeIndex = memory_type(
          req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      V(vkAllocateMemory(device, &ai, nullptr, &b.memory));
      V(vkBindBufferMemory(device, b.handle, b.memory, 0));
      V(vkMapMemory(device, b.memory, 0, size, 0, &b.mapping));
      if (address) {
        VkBufferDeviceAddressInfo a{
            VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        a.buffer = b.handle;
        b.address = vkGetBufferDeviceAddress(device, &a);
      }
      buffers.push_back(b);
      return b;
    };
    std::vector<Image> images;
    auto image = [&](uint32_t w, uint32_t h, VkImageUsageFlags usage) {
      Image r;
      VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
      ci.imageType = VK_IMAGE_TYPE_2D;
      ci.format = VK_FORMAT_R32G32B32A32_SFLOAT;
      ci.extent = {w, h, 1};
      ci.mipLevels = 1;
      ci.arrayLayers = 1;
      ci.samples = VK_SAMPLE_COUNT_1_BIT;
      ci.tiling = VK_IMAGE_TILING_OPTIMAL;
      ci.usage = usage;
      V(vkCreateImage(device, &ci, nullptr, &r.handle));
      VkMemoryRequirements req;
      vkGetImageMemoryRequirements(device, r.handle, &req);
      VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      ai.allocationSize = req.size;
      ai.memoryTypeIndex = memory_type(req.memoryTypeBits, 0);
      V(vkAllocateMemory(device, &ai, nullptr, &r.memory));
      V(vkBindImageMemory(device, r.handle, r.memory, 0));
      VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      vi.image = r.handle;
      vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
      vi.format = ci.format;
      vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      V(vkCreateImageView(device, &vi, nullptr, &r.view));
      images.push_back(r);
      return r;
    };
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSampler sampler;
    V(vkCreateSampler(device, &si, nullptr, &sampler));
    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
    for (uint32_t i = 0; i < 4; ++i)
      bindings[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                     VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo dl{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = 4;
    dl.pBindings = bindings.data();
    VkDescriptorSetLayout layout;
    V(vkCreateDescriptorSetLayout(device, &dl, nullptr, &layout));
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16};
    VkDescriptorPoolCreateInfo dpi{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 4;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &size;
    VkDescriptorPool dp;
    V(vkCreateDescriptorPool(device, &dpi, nullptr, &dp));
    std::array<VkDescriptorSetLayout, 4> layouts{layout, layout, layout,
                                                 layout};
    std::array<VkDescriptorSet, 4> sets{};
    VkDescriptorSetAllocateInfo da{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    da.descriptorPool = dp;
    da.descriptorSetCount = 4;
    da.pSetLayouts = layouts.data();
    V(vkAllocateDescriptorSets(device, &da, sets.data()));
    VkPushConstantRange pr{VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(SplitPostFxPushConstants)};
    VkPipelineLayoutCreateInfo pli{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &layout;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pr;
    VkPipelineLayout pl;
    V(vkCreatePipelineLayout(device, &pli, nullptr, &pl));
    auto module = [&](const uint32_t *words, size_t bytes) {
      VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
      ci.codeSize = bytes;
      ci.pCode = words;
      VkShaderModule m;
      V(vkCreateShaderModule(device, &ci, nullptr, &m));
      return m;
    };
    auto reference = argc == 4 ? Spirv(argv[3]) : std::vector<uint32_t>{};
    auto vm = module(fullscreen_cw_vs, sizeof(fullscreen_cw_vs)),
         fm = argc == 4 ? module(reference.data(),
                                 reference.size() * sizeof(uint32_t))
                        : module(gta4_native_split_postfx_ps,
                                 sizeof(gta4_native_split_postfx_ps));
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    for (auto &s : stages) {
      s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      s.pName = "main";
    }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vm;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fm;
    VkPipelineVertexInputStateCreateInfo vi{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vs{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vs.viewportCount = vs.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState ba{};
    ba.colorWriteMask = 15;
    VkPipelineColorBlendStateCreateInfo bs{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    bs.attachmentCount = 1;
    bs.pAttachments = &ba;
    const VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                       VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamics;
    const VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
    VkPipelineRenderingCreateInfo rendering{
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
    VkGraphicsPipelineCreateInfo gi{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gi.pNext = &rendering;
    gi.stageCount = 2;
    gi.pStages = stages.data();
    gi.pVertexInputState = &vi;
    gi.pInputAssemblyState = &ia;
    gi.pViewportState = &vs;
    gi.pRasterizationState = &rs;
    gi.pMultisampleState = &ms;
    gi.pColorBlendState = &bs;
    gi.pDynamicState = &dynamic;
    gi.layout = pl;
    VkPipeline pipeline;
    V(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gi, nullptr,
                                &pipeline));
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = family;
    VkCommandPool pool;
    V(vkCreateCommandPool(device, &pci, nullptr, &pool));
    VkCommandBufferAllocateInfo cai{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cb;
    V(vkAllocateCommandBuffers(device, &cai, &cb));
    std::vector<std::filesystem::path> cases;
    for (auto &entry : std::filesystem::directory_iterator(argv[1]))
      if (entry.path().extension() == ".input")
        cases.push_back(entry.path());
    std::sort(cases.begin(), cases.end());
    Check(!cases.empty(), "no fixtures");
    for (const auto &path : cases) {
      auto data = Read(path.string());
      Check(data.size() >= 56, "fixture header");
      uint32_t width, height;
      std::memcpy(&width, data.data(), 4);
      std::memcpy(&height, data.data() + 4, 4);
      Check(width && height && width <= 8192 && height <= 8192,
            "fixture dimensions");
      SplitPostFxPushConstants push{};
      push.full_extent[0] = width;
      push.full_extent[1] = height;
      std::memcpy(push.dof_projection, data.data() + 8, 48);
      const bool zero = push.dof_blur[0] == 0 && push.dof_blur[1] == 0 &&
                        push.dof_blur[2] == 0;
      std::array<Image, 4> inputs, targets;
      size_t offset = 56;
      auto upload =
          buffer(data.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false);
      std::memcpy(upload.mapping, data.data(), data.size());
      auto readback = buffer(size_t(width) * height * 16,
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
      V(vkResetCommandBuffer(cb, 0));
      VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      V(vkBeginCommandBuffer(cb, &bi));
      auto transition = [&](Image im, VkImageLayout from, VkImageLayout to,
                            VkAccessFlags source, VkAccessFlags dest,
                            VkPipelineStageFlags before,
                            VkPipelineStageFlags after) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcAccessMask = source;
        b.dstAccessMask = dest;
        b.oldLayout = from;
        b.newLayout = to;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = im.handle;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cb, before, after, 0, 0, nullptr, 0, nullptr, 1,
                             &b);
      };
      for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t w = i == 1 ? width / 2 : width,
                       h = i == 1 ? height / 2 : height;
        inputs[i] = image(
            w, h, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        Check(offset + size_t(w) * h * 16 <= data.size(), "fixture payload");
        transition(inputs[i], VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                   VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy{};
        copy.bufferOffset = offset;
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {w, h, 1};
        vkCmdCopyBufferToImage(cb, upload.handle, inputs[i].handle,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        offset += size_t(w) * h * 16;
        transition(inputs[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        const bool half = i == 1 || i == 2;
        targets[i] = image(half ? width / 2 : width, half ? height / 2 : height,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                               VK_IMAGE_USAGE_SAMPLED_BIT |
                               VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
      }
      for (uint32_t pass = 0; pass < (zero ? 1u : 4u); ++pass) {
        const uint32_t w = (pass == 1 || pass == 2) ? width / 2 : width,
                       h = (pass == 1 || pass == 2) ? height / 2 : height;
        Image source = pass == 0   ? inputs[0]
                       : pass == 1 ? inputs[1]
                       : pass == 2 ? targets[1]
                                   : targets[0];
        std::array<Image, 4> sampled{source, pass == 3 ? targets[2] : source,
                                     inputs[2], inputs[3]};
        std::array<VkDescriptorImageInfo, 4> info{};
        std::array<VkWriteDescriptorSet, 4> writes{};
        for (uint32_t i = 0; i < 4; ++i) {
          info[i] = {sampler, sampled[i].view,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
          writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
          writes[i].dstSet = sets[pass];
          writes[i].dstBinding = i;
          writes[i].descriptorCount = 1;
          writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
          writes[i].pImageInfo = &info[i];
        }
        vkUpdateDescriptorSets(device, 4, writes.data(), 0, nullptr);
        transition(targets[pass], VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0,
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        VkRenderingAttachmentInfo attachment{
            VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        attachment.imageView = targets[pass].view;
        attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo begin{VK_STRUCTURE_TYPE_RENDERING_INFO};
        begin.renderArea.extent = {w, h};
        begin.layerCount = 1;
        begin.colorAttachmentCount = 1;
        begin.pColorAttachments = &attachment;
        vkCmdBeginRendering(cb, &begin);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1,
                                &sets[pass], 0, nullptr);
        push.pass_index = pass;
        push.destination_extent[0] = w;
        push.destination_extent[1] = h;
        push.source_extent[0] = (pass == 1 || pass == 2) ? width / 2 : width;
        push.source_extent[1] = (pass == 1 || pass == 2) ? height / 2 : height;
        VkViewport viewport{0, 0, float(w), float(h), 0, 1};
        VkRect2D scissor{{0, 0}, {w, h}};
        vkCmdSetViewport(cb, 0, 1, &viewport);
        vkCmdSetScissor(cb, 0, 1, &scissor);
        vkCmdPushConstants(cb, pl, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(push), &push);
        vkCmdDraw(cb, 3, 1, 0, 0);
        vkCmdEndRendering(cb);
        transition(targets[pass], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                   VK_ACCESS_SHADER_READ_BIT,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
      }
      auto result = targets[zero ? 0 : 3];
      transition(result, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT);
      VkBufferImageCopy copy{};
      copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      copy.imageExtent = {width, height, 1};
      vkCmdCopyImageToBuffer(cb, result.handle,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             readback.handle, 1, &copy);
      VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
      barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex =
          VK_QUEUE_FAMILY_IGNORED;
      barrier.buffer = readback.handle;
      barrier.size = VK_WHOLE_SIZE;
      vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                           &barrier, 0, nullptr);
      V(vkEndCommandBuffer(cb));
      VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
      submit.commandBufferCount = 1;
      submit.pCommandBuffers = &cb;
      V(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
      V(vkQueueWaitIdle(queue));
      std::ofstream raw(output / (path.stem().string() + ".rgba32f"),
                        std::ios::binary);
      raw.write((char *)readback.mapping, size_t(width) * height * 16);
      Check(bool(raw), "result write");
      std::cout << path.stem().string() << ": complete\n";
    }
    V(vkDeviceWaitIdle(device));
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyShaderModule(device, vm, nullptr);
    vkDestroyShaderModule(device, fm, nullptr);
    vkDestroyCommandPool(device, pool, nullptr);
    vkDestroyPipelineLayout(device, pl, nullptr);
    vkDestroyDescriptorPool(device, dp, nullptr);
    vkDestroyDescriptorSetLayout(device, layout, nullptr);
    vkDestroySampler(device, sampler, nullptr);
    for (auto &i : images) {
      vkDestroyImageView(device, i.view, nullptr);
      vkDestroyImage(device, i.handle, nullptr);
      vkFreeMemory(device, i.memory, nullptr);
    }
    for (auto &b : buffers) {
      vkUnmapMemory(device, b.memory);
      vkDestroyBuffer(device, b.handle, nullptr);
      vkFreeMemory(device, b.memory, nullptr);
    }
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL " << e.what() << '\n';
    return 1;
  }
}
