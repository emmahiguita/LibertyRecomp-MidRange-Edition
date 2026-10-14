#include "vulkan_frame_generation.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <rex/assert.h>
#include <rex/ui/vulkan/util.h>

#include "dlss_frame_generation.h"
#include "fsr_vulkan.h"

namespace rex::graphics::gta4_native::temporal {
namespace {
constexpr VkFormat kFormat = ui::vulkan::VulkanPresenter::kGuestOutputFormat;
constexpr VkImageUsageFlags kUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
// Mailboxes + in-flight display submissions + retained real + current encoding.
// Derived with Python from the presenter's bounded rings. Allocation is lazy.
constexpr size_t kMaximumRetainedPairs = 8;

bool WaitIdle(const ui::vulkan::VulkanDevice& device, bool* device_lost = nullptr) {
  std::vector<ui::vulkan::VulkanDevice::Queue::Acquisition> locks;
  for (const auto& family : device.queue_families())
    for (const auto& queue : family.queues) locks.emplace_back(queue->Acquire());
  const auto result = device.functions().vkDeviceWaitIdle(device.device());
  if (device_lost) *device_lost = result == VK_ERROR_DEVICE_LOST;
  return result == VK_SUCCESS;
}

bool SupportsFrameImages(const ui::vulkan::VulkanDevice& device) {
  VkFormatProperties properties{};
  device.vulkan_instance()->functions().vkGetPhysicalDeviceFormatProperties(
      device.physical_device(), kFormat, &properties);
  constexpr VkFormatFeatureFlags required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
      VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
      VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
  return (properties.optimalTilingFeatures & required) == required;
}

VkImageMemoryBarrier Barrier(const Image& image, VkImageLayout old_layout, VkImageLayout new_layout,
                             VkAccessFlags source_access, VkAccessFlags destination_access) {
  VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.srcAccessMask = source_access;
  barrier.dstAccessMask = destination_access;
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image.image;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  return barrier;
}

DlssFrameGenerationInput DlssInput(const Image& present, const Image& generated,
                                  const FrameGenerationCamera& camera) {
  DlssFrameGenerationInput input;
  input.present_color = present;
  input.generated_output = generated;
  input.camera_view_to_clip = camera.view_to_clip;
  input.clip_to_camera_view = camera.clip_to_view;
  input.clip_to_previous_clip = camera.clip_to_previous;
  input.previous_clip_to_clip = camera.previous_to_clip;
  input.camera_position = camera.position;
  input.camera_up = camera.up;
  input.camera_right = camera.right;
  input.camera_forward = camera.forward;
  input.projection_jitter = camera.projection_jitter;
  input.camera_aspect_ratio = camera.aspect_ratio;
  input.camera_valid = camera.valid;
  return input;
}
}  // namespace

struct FrameGenerationResources {
  const ui::vulkan::VulkanDevice* device = nullptr;
  // The pool holds a reference until GPU retirement. Published outputs keep the
  // allocation alive after pool retirement until their display fences complete.
  std::array<Image, 4> images{};
  std::array<VkDeviceMemory, 4> memory{};
  uint64_t sequence = 0;
  bool encoded = false, generated = false, submitted = false, finished = false;

  ~FrameGenerationResources() {
    if (!device) return;
    const auto& fn = device->functions();
    for (size_t index = 0; index < images.size(); ++index) {
      if (images[index].view) fn.vkDestroyImageView(device->device(), images[index].view, nullptr);
      if (images[index].image) fn.vkDestroyImage(device->device(), images[index].image, nullptr);
      if (memory[index]) fn.vkFreeMemory(device->device(), memory[index], nullptr);
    }
  }

  static std::shared_ptr<FrameGenerationResources> Create(
      const ui::vulkan::VulkanDevice& device, Extent extent) {
    auto resources = std::make_shared<FrameGenerationResources>();
    resources->device = &device;
    const std::array families{device.queue_family_graphics_compute(),
                              device.queue_family_native_offscreen()};
    const bool concurrent = families.front() != families.back();
    VkImageCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    create.imageType = VK_IMAGE_TYPE_2D;
    create.format = kFormat;
    create.extent = {extent.width, extent.height, 1};
    create.mipLevels = create.arrayLayers = 1;
    create.samples = VK_SAMPLE_COUNT_1_BIT;
    create.tiling = VK_IMAGE_TILING_OPTIMAL;
    create.usage = kUsage;
    create.sharingMode = concurrent ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
    create.queueFamilyIndexCount = concurrent ? uint32_t(families.size()) : 0;
    create.pQueueFamilyIndices = concurrent ? families.data() : nullptr;
    create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    for (size_t index = 0; index < resources->images.size(); ++index) {
      auto& image = resources->images[index];
      image.format = kFormat;
      image.extent = extent;
      image.usage = kUsage;
      image.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
      if (!ui::vulkan::util::CreateDedicatedAllocationImage(
              &device, create, ui::vulkan::util::MemoryPurpose::kDeviceLocal,
              image.image, resources->memory[index])) return {};
      VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      view.image = image.image;
      view.viewType = VK_IMAGE_VIEW_TYPE_2D;
      view.format = kFormat;
      view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      if (device.functions().vkCreateImageView(device.device(), &view, nullptr, &image.view) != VK_SUCCESS)
        return {};
    }
    return resources;
  }
};

struct VulkanFrameGeneration::Impl {
  const ui::vulkan::VulkanDevice* device = nullptr;
  FrameGenerationProvider provider = FrameGenerationProvider::kFsr;
  Configuration configuration{};
  bool configured = false, hdr_scene = false, reset = true, recreate = false;
  bool failed_configuration = false;
  FsrFrameGenerationVulkan fsr;
  DlssFrameGenerator dlss;
  std::vector<std::shared_ptr<FrameGenerationResources>> pool;
  std::shared_ptr<FrameGenerationResources> pending;
  std::string status = "Vulkan frame generation has not been initialized";

  std::shared_ptr<FrameGenerationResources> Acquire() {
    for (const auto& resources : pool) {
      if (resources.use_count() == 1) return resources;
    }
    if (pool.size() >= kMaximumRetainedPairs) {
      status = "Frame-generation output images are still retained by rendering or presentation";
      return {};
    }
    auto resources = FrameGenerationResources::Create(*device, configuration.output_extent);
    if (resources) pool.push_back(resources);
    else status = "Failed to allocate immutable Vulkan frame-generation images";
    return resources;
  }
};

VulkanFrameGeneration::VulkanFrameGeneration() : impl_(std::make_unique<Impl>()) {}
VulkanFrameGeneration::~VulkanFrameGeneration() { Shutdown(); }
bool VulkanFrameGeneration::available() const { return impl_->configured && !impl_->recreate; }
const std::string& VulkanFrameGeneration::status() const { return impl_->status; }

bool VulkanFrameGeneration::SupportsProvider(const ui::vulkan::VulkanDevice& device,
                                              FrameGenerationProvider provider,
                                              std::string* reason) {
  if (!SupportsFrameImages(device)) {
    if (reason) *reason = "Selected Vulkan device cannot store, draw, sample and copy FP16 generated frames";
    return false;
  }
  if (provider == FrameGenerationProvider::kDlss) {
    DlssFrameGenerator probe;
    const bool supported = probe.Initialize(device);
    if (reason) *reason = supported ? std::string{} : probe.status();
    return supported;
  }
  if (!FsrSupportsDevice(device, reason, true)) return false;
  FsrFrameGenerationVulkan probe;
  // Python: the pinned optical-flow input pyramid's largest shift is six;
  // 128 leaves its smallest image two pixels wide, avoiding zero-size probes.
  const Configuration configuration{{128, 128}, {128, 128}, Quality::kNative, false, false, false};
  const bool supported = probe.Initialize(device.physical_device(), device.device(),
      device.vulkan_instance()->functions().vkGetDeviceProcAddr) &&
      probe.Configure(configuration, kFormat);
  if (reason) *reason = supported ? std::string{} : probe.last_error();
  return supported;
}

bool VulkanFrameGeneration::Initialize(const ui::vulkan::VulkanDevice& device) {
  if (impl_->device == &device) return true;
  Shutdown();
  impl_->device = &device;
  if (!SupportsFrameImages(device)) {
    impl_->status = "Selected Vulkan device cannot store, draw, sample and copy FP16 generated frames";
    impl_->device = nullptr;
    return false;
  }
  impl_->status = "Vulkan frame-generation resources ready";
  return true;
}

bool VulkanFrameGeneration::Configure(FrameGenerationProvider provider,
                                      const Configuration& configuration, bool hdr_scene) {
  auto& state = *impl_;
  if (!state.device || !configuration.render_extent || !configuration.output_extent || state.pending) {
    state.status = "Frame generation needs a device, valid extents, and a resolved prior submission";
    return false;
  }
  Configuration actual = configuration;
  actual.hdr = hdr_scene;
  const bool unchanged = provider == state.provider && actual == state.configuration;
  if (unchanged && state.failed_configuration) return false;
  if (unchanged && state.configured && !state.recreate) return true;
  if (!WaitIdle(*state.device)) {
    state.status = "Vulkan frame-generation work did not retire before configuration";
    return false;
  }
  if (!unchanged) {
    state.fsr.Shutdown();
    state.dlss.Shutdown();
    state.pool.clear();
  }
  state.provider = provider;
  state.configuration = actual;
  state.hdr_scene = hdr_scene;
  state.configured = false;
  state.failed_configuration = true;
  state.reset = true;
  bool success = false;
  if (provider == FrameGenerationProvider::kDlss) {
    success = state.dlss.Initialize(*state.device) && state.dlss.Configure(actual, kFormat, hdr_scene);
    if (!success) state.status = state.dlss.status();
  } else {
    if (!FsrSupportsDevice(*state.device, &state.status, true)) return false;
    success = state.fsr.Initialize(state.device->physical_device(), state.device->device(),
        state.device->vulkan_instance()->functions().vkGetDeviceProcAddr) &&
        state.fsr.Configure(actual, kFormat);
    if (!success) state.status = state.fsr.last_error();
  }
  if (!success) return false;
  state.configured = true;
  state.failed_configuration = state.recreate = false;
  state.status = provider == FrameGenerationProvider::kDlss ? "DLSS frame generation configured"
                                                           : "FSR frame generation configured";
  return true;
}

bool VulkanFrameGeneration::Encode(VkCommandBuffer commands, const FrameInput& frame,
                                   const Image& present, const FrameGenerationCamera& camera,
                                   const ui::vulkan::VulkanPresenter& presenter,
                                   FrameGenerationOutput& output) {
  auto& state = *impl_;
  // A second composite can be observed while this command buffer still owns
  // the first interpolation. Preserve its lease until submission/discard even
  // when the caller attempts to encode the next candidate into the same output.
  if (state.pending) {
    state.status = "Frame generation is awaiting its recorded scene submission";
    state.reset = true;
    return false;
  }
  output = {};
  if (!state.configured || state.recreate || !commands) return false;
  if (!presenter.CanPresentGeneratedFrames()) {
    state.status = "Frame generation requires a connected FIFO presentation surface";
    state.reset = true;
    return false;
  }
  const double half_interval = double(frame.frame_time_ms) * 500'000.0;
  if (!ValidFrameInput(frame) || frame.render_extent != state.configuration.render_extent ||
      frame.output_extent != state.configuration.output_extent || !present ||
      present.format != kFormat || present.extent != frame.output_extent ||
      present.layout != VK_IMAGE_LAYOUT_GENERAL || present.aspect != VK_IMAGE_ASPECT_COLOR_BIT ||
      !(present.usage & VK_IMAGE_USAGE_SAMPLED_BIT) || !(present.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ||
      !camera.valid || !std::isfinite(camera.view_space_to_meters) || camera.view_space_to_meters <= 0.0f ||
      !std::isfinite(camera.minimum_luminance) || camera.minimum_luminance < 0.0f ||
      !std::isfinite(camera.maximum_luminance) || camera.maximum_luminance <= camera.minimum_luminance ||
      !std::isfinite(half_interval) || half_interval < 1.0 || half_interval > 100'000'000.0) {
    state.status = "Frame generation requires complete temporal, camera and display-color inputs";
    state.reset = true;
    return false;
  }
  auto resources = state.Acquire();
  if (!resources) { state.reset = true; return false; }
  for (auto& image : resources->images) image.layout = VK_IMAGE_LAYOUT_GENERAL;
  auto dlss_input = DlssInput(present, resources->images[0], camera);
  if (!ValidDlssFrameGenerationInput(frame, dlss_input, kFormat)) {
    state.status = "Frame-generation camera transforms, image formats or aliases are invalid";
    state.reset = true;
    return false;
  }
  resources->sequence = frame.sequence;
  resources->encoded = resources->generated = resources->submitted = resources->finished = false;
  state.pending = resources;
  output.resources_ = resources;
  output.generated = resources->images[0];
  output.real = resources->images[1];
  output.generated_present = resources->images[2];
  output.real_present = resources->images[3];
  output.sequence = frame.sequence;
  output.epoch = frame.epoch;
  output.interval_ns = uint64_t(half_interval);

  std::array<VkImageMemoryBarrier, 5> prepare{};
  for (size_t index = 0; index < resources->images.size(); ++index)
    prepare[index] = Barrier(resources->images[index], VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
  prepare.back() = Barrier(present, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
      VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT);
  const auto& fn = state.device->functions();
  fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr,
      uint32_t(prepare.size()), prepare.data());

  auto evaluated_frame = frame;
  evaluated_frame.reset |= state.reset;
  bool encoded = false;
  if (state.provider == FrameGenerationProvider::kDlss) {
    encoded = state.dlss.Encode(commands, evaluated_frame, dlss_input);
    if (!encoded) state.status = state.dlss.status();
    resources->generated = encoded && state.dlss.has_generated_frame();
  } else {
    FsrFrameGenerationInput input;
    input.present_color = present;
    input.generated_output = output.generated;
    input.transfer_function = state.hdr_scene ? FsrTransferFunction::kScRgb : FsrTransferFunction::kSrgb;
    input.minimum_luminance = camera.minimum_luminance;
    input.maximum_luminance = camera.maximum_luminance;
    input.view_space_to_meters = camera.view_space_to_meters;
    std::copy(camera.position.begin(), camera.position.end(), input.camera_position);
    std::copy(camera.up.begin(), camera.up.end(), input.camera_up);
    std::copy(camera.right.begin(), camera.right.end(), input.camera_right);
    std::copy(camera.forward.begin(), camera.forward.end(), input.camera_forward);
    encoded = state.fsr.Encode(commands, evaluated_frame, input);
    if (!encoded) state.status = state.fsr.last_error();
    resources->generated = encoded && state.fsr.has_generated_frame();
  }
  if (!encoded) {
    state.recreate = true;
    state.reset = true;
    return false;
  }
  resources->encoded = true;
  state.reset = false;
  const auto copy_ready = Barrier(present, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
      VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
  fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
      0, 0, nullptr, 0, nullptr, 1, &copy_ready);
  VkImageCopy copy{};
  copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.extent = {present.extent.width, present.extent.height, 1};
  fn.vkCmdCopyImage(commands, present.image, VK_IMAGE_LAYOUT_GENERAL,
      output.real.image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
  const std::array hud_ready{
      Barrier(output.generated, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
          VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT),
      Barrier(output.real, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
          VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT)};
  fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr,
      uint32_t(hud_ready.size()), hud_ready.data());
  output.has_generated_frame = resources->generated;
  state.status = resources->generated ? "Generated and retained real scenes encoded; HUD replay required"
                                      : "Frame-generation history seeded";
  return true;
}

bool VulkanFrameGeneration::FinishForPresentation(VkCommandBuffer commands, FrameGenerationOutput& output) {
  auto& state = *impl_;
  const auto& resources = output.resources_;
  if (!commands || !resources || resources != state.pending || !resources->encoded ||
      resources->sequence != output.sequence || resources->finished ||
      output.generated_present.image != resources->images[2].image ||
      output.real_present.image != resources->images[3].image ||
      output.generated_present.view != resources->images[2].view ||
      output.real_present.view != resources->images[3].view ||
      output.generated_present.layout == VK_IMAGE_LAYOUT_UNDEFINED ||
      output.real_present.layout == VK_IMAGE_LAYOUT_UNDEFINED) return false;
  const auto destination = ui::vulkan::VulkanPresenter::kGuestOutputInternalLayout;
  const std::array barriers{
      Barrier(output.generated_present, output.generated_present.layout, destination,
          VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT),
      Barrier(output.real_present, output.real_present.layout, destination,
          VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)};
  state.device->functions().vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      ui::vulkan::VulkanPresenter::kGuestOutputInternalStageMask, 0, 0, nullptr, 0, nullptr,
      uint32_t(barriers.size()), barriers.data());
  output.generated_present.layout = output.real_present.layout = destination;
  resources->finished = true;
  return true;
}

void VulkanFrameGeneration::OnSubmitted(uint64_t sequence) {
  auto& state = *impl_;
  if (!state.pending) return;
  const auto resources = std::move(state.pending);
  if (sequence != resources->sequence) {
    if (state.provider == FrameGenerationProvider::kDlss) state.dlss.OnSubmitted(sequence);
    else state.fsr.OnDiscarded();
    state.recreate = state.reset = true;
    state.status = "Frame-generation submission identity does not match the encoded scene";
    return;
  }
  resources->submitted = true;
  if (resources->encoded) {
    if (state.provider == FrameGenerationProvider::kDlss) state.dlss.OnSubmitted(sequence);
    else state.fsr.OnSubmitted();
  }
}

void VulkanFrameGeneration::OnDiscarded() {
  auto& state = *impl_;
  if (!state.pending) return;
  if (state.provider == FrameGenerationProvider::kDlss) state.dlss.OnDiscarded();
  else state.fsr.OnDiscarded();
  state.pending->encoded = state.pending->generated = state.pending->finished = false;
  state.pending.reset();
  state.recreate = state.reset = true;
}

bool VulkanFrameGeneration::Publish(
    ui::vulkan::VulkanPresenter::VulkanGuestOutputRefreshContext& context,
    const FrameGenerationOutput& output) const {
  const auto& resources = output.resources_;
  if (!output.has_generated_frame || !resources || !resources->submitted ||
      !resources->finished || !resources->generated ||
      resources->sequence != output.sequence || !context.image_access_submitted()) return false;
  return context.SetGeneratedFrame(resources->images[2].image, resources->images[2].view,
      resources->images[3].image, resources->images[3].view,
      {resources->images[2].extent.width, resources->images[2].extent.height}, resources,
      output.epoch, output.interval_ns);
}

void VulkanFrameGeneration::Reset() { impl_->reset = true; }

void VulkanFrameGeneration::Shutdown() {
  bool device_lost = false;
  if (impl_->device && !WaitIdle(*impl_->device, &device_lost) && !device_lost) {
    rex::FatalError("Frame-generation shutdown cannot establish GPU completion; refusing unsafe resource and device destruction");
  }
  impl_->fsr.Shutdown();
  impl_->dlss.Shutdown();
  impl_->pending.reset();
  impl_->pool.clear();
  impl_ = std::make_unique<Impl>();
}

}  // namespace rex::graphics::gta4_native::temporal
