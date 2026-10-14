#include "dlss_frame_generation.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include <rex/assert.h>
#include <rex/logging.h>
#include <rex/ui/vulkan/device.h>

#include "dlss_sdk.h"
#if defined(REX_HAS_DLSS_NGX)
#include <nvsdk_ngx_helpers_dlssg_vk.h>
#endif

namespace rex::graphics::gta4_native::temporal {
namespace {
bool InversePair(const std::array<float, 16>& a, const std::array<float, 16>& b) {
  for (float v : a) if (!std::isfinite(v)) return false;
  for (float v : b) if (!std::isfinite(v)) return false;
  for (size_t row = 0; row < 4; ++row) {
    for (size_t col = 0; col < 4; ++col) {
      double sum = 0.0;
      for (size_t k = 0; k < 4; ++k) sum += double(a[row * 4 + k]) * b[k * 4 + col];
      if (std::abs(sum - (row == col ? 1.0 : 0.0)) > 0.01) return false;
    }
  }
  return true;
}

bool UnitVector(const std::array<float, 3>& vector) {
  double length_squared = 0.0;
  for (float v : vector) {
    if (!std::isfinite(v)) return false;
    length_squared += double(v) * v;
  }
  return std::abs(length_squared - 1.0) <= 0.01;
}

bool Perpendicular(const std::array<float, 3>& a, const std::array<float, 3>& b) {
  double dot = 0.0;
  for (size_t i = 0; i < a.size(); ++i) dot += double(a[i]) * b[i];
  return std::abs(dot) <= 0.01;
}

bool SupportedPresentFormat(VkFormat format, bool hdr) {
  if (hdr) {
    return format == VK_FORMAT_R16G16B16A16_SFLOAT ||
           format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ||
           format == VK_FORMAT_A2R10G10B10_UNORM_PACK32;
  }
  return format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_B8G8R8A8_UNORM ||
         format == VK_FORMAT_R16G16B16A16_SFLOAT;
}

#if defined(REX_HAS_DLSS_NGX)
NVSDK_NGX_Resource_VK NgxImage(const Image& image, bool writable) {
  const VkImageSubresourceRange range{image.aspect, 0, 1, 0, 1};
  return NVSDK_NGX_Create_ImageView_Resource_VK(image.view, image.image, range, image.format,
                                              image.extent.width, image.extent.height, writable);
}
#endif
}  // namespace

bool ValidDlssFrameGenerationInput(const FrameInput& frame, const DlssFrameGenerationInput& input,
                                  VkFormat format) {
  const auto readable = [](const Image& image) {
    return image && image.layout == VK_IMAGE_LAYOUT_GENERAL &&
           image.aspect == VK_IMAGE_ASPECT_COLOR_BIT &&
           (image.usage & VK_IMAGE_USAGE_SAMPLED_BIT);
  };
  const auto output = [&](const Image& image) {
    return image && image.layout == VK_IMAGE_LAYOUT_GENERAL &&
           image.aspect == VK_IMAGE_ASPECT_COLOR_BIT &&
           (image.usage & VK_IMAGE_USAGE_STORAGE_BIT) && image.extent == frame.output_extent &&
           image.format == format && image.image != input.present_color.image &&
           image.image != frame.color.image && image.image != frame.depth.image &&
           image.image != frame.motion.image && image.image != frame.reactive.image &&
           image.image != frame.composition.image && image.image != frame.exposure.image;
  };
  if (!ValidFrameInput(frame) || !input.camera_valid || !readable(input.present_color) ||
      input.present_color.image == frame.depth.image || input.present_color.image == frame.motion.image ||
      input.present_color.extent != frame.output_extent || input.present_color.format != format ||
      !output(input.generated_output) ||
      ((input.retained_real_output.image || input.retained_real_output.view) &&
       (!output(input.retained_real_output) ||
        input.retained_real_output.image == input.generated_output.image)) ||
      !InversePair(input.camera_view_to_clip, input.clip_to_camera_view) ||
      !InversePair(input.clip_to_previous_clip, input.previous_clip_to_clip) ||
      !UnitVector(input.camera_up) || !UnitVector(input.camera_right) ||
      !UnitVector(input.camera_forward) || !Perpendicular(input.camera_up, input.camera_right) ||
      !Perpendicular(input.camera_up, input.camera_forward) ||
      !Perpendicular(input.camera_right, input.camera_forward) ||
      !std::isfinite(input.camera_aspect_ratio) ||
      input.camera_aspect_ratio <= 0.0f) return false;
  if (frame.motion.format != VK_FORMAT_R16G16_SFLOAT &&
      frame.motion.format != VK_FORMAT_R32G32_SFLOAT) return false;
  if (frame.motion.aspect != VK_IMAGE_ASPECT_COLOR_BIT ||
      frame.depth.aspect != (frame.depth.format == VK_FORMAT_R32_SFLOAT
          ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT)) return false;
  if (frame.depth.format != VK_FORMAT_R32_SFLOAT && frame.depth.format != VK_FORMAT_D16_UNORM &&
      frame.depth.format != VK_FORMAT_D24_UNORM_S8_UINT && frame.depth.format != VK_FORMAT_D32_SFLOAT &&
      frame.depth.format != VK_FORMAT_D32_SFLOAT_S8_UINT) return false;
  if (std::abs(frame.jitter_x) > 0.5f || std::abs(frame.jitter_y) > 0.5f) return false;
  for (float v : input.camera_position) if (!std::isfinite(v)) return false;
  for (float v : input.projection_jitter) if (!std::isfinite(v)) return false;
  return true;
}

struct DlssFrameGenerator::Impl {
  const ui::vulkan::VulkanDevice* device = nullptr;
  Configuration configuration{};
  VkFormat format = VK_FORMAT_UNDEFINED;
  bool hdr = false;
  bool available = false;
  bool configured = false;
  bool failed_configuration = false;
  bool invalid_feature = false;
  bool pending = false;
  bool history = false;
  bool generated = false;
  uint64_t pending_sequence = 0;
  uint64_t pending_epoch = 0;
  uint64_t last_sequence = 0;
  uint64_t last_epoch = 0;
  std::string status = "DLSS frame generation has not been initialized";
#if defined(REX_HAS_DLSS_NGX)
  std::shared_ptr<DlssDiscovery> discovery;
  NVSDK_NGX_Parameter* parameters = nullptr;
  NVSDK_NGX_Handle* feature = nullptr;
  bool initialized = false;

  void ReleaseFeature() {
    if (feature) NVSDK_NGX_VULKAN_ReleaseFeature(feature);
    feature = nullptr;
    configured = false;
    history = false;
    generated = false;
  }
#endif
};

DlssFrameGenerator::DlssFrameGenerator() : impl_(std::make_unique<Impl>()) {}
DlssFrameGenerator::~DlssFrameGenerator() { Shutdown(); }
bool DlssFrameGenerator::available() const { return impl_->available; }
bool DlssFrameGenerator::has_generated_frame() const { return impl_->generated; }
const std::string& DlssFrameGenerator::status() const { return impl_->status; }

bool DlssFrameGenerator::Initialize(const ui::vulkan::VulkanDevice& device) {
  if (impl_->device == &device) return impl_->available;
  Shutdown();
  impl_->device = &device;
  if (!device.dlss_frame_generation_extensions_enabled()) {
    impl_->status = device.dlss_frame_generation_unavailable_reason();
    return false;
  }
#if defined(REX_HAS_DLSS_NGX)
  std::lock_guard lock(DlssSdkMutex());
  impl_->discovery = std::make_shared<DlssDiscovery>(DlssFeature::kFrameGeneration);
  if (!impl_->discovery->error.empty()) {
    impl_->status = impl_->discovery->error;
    return false;
  }
  if (!AcquireDlssDevice(device, impl_->discovery, impl_->status)) return false;
  impl_->initialized = true;
  const auto result = NVSDK_NGX_VULKAN_GetCapabilityParameters(&impl_->parameters);
  if (NVSDK_NGX_FAILED(result) || !impl_->parameters) {
    impl_->status = DlssResultMessage("DLSS FG capability parameters", result);
    return false;
  }
  int supported = 0;
  const auto capability = impl_->parameters->Get(NVSDK_NGX_Parameter_FrameGeneration_Available,
                                                 &supported);
  if (NVSDK_NGX_FAILED(capability) || !supported) {
    impl_->status = "NGX reports DLSS frame generation unavailable on the selected device";
    return false;
  }
  impl_->available = true;
  impl_->status = "DLSS frame generation ready (SDK " + std::string(kDlssSdkVersion) + ")";
  return true;
#else
  impl_->status = "DLSS SDK is not included in this build";
  return false;
#endif
}

bool DlssFrameGenerator::Configure(const Configuration& configuration, VkFormat format, bool hdr) {
  if (!impl_->available || !configuration.render_extent || !configuration.output_extent ||
      !SupportedPresentFormat(format, hdr)) return false;
  if (impl_->pending) {
    impl_->status = "DLSS FG cannot reconfigure while an encoded frame awaits submission";
    return false;
  }
  if (impl_->configuration == configuration && impl_->format == format && impl_->hdr == hdr) {
    if (impl_->failed_configuration) return false;
    if (impl_->configured && !impl_->invalid_feature) return true;
  }
#if defined(REX_HAS_DLSS_NGX)
  impl_->configuration = configuration;
  impl_->format = format;
  impl_->hdr = hdr;
  impl_->failed_configuration = true;
  impl_->generated = false;
  if (!WaitDlssDeviceIdle(*impl_->device)) {
    impl_->status = "Vulkan device did not retire DLSS FG work before reconfiguration";
    return false;
  }
  std::lock_guard lock(DlssSdkMutex());
  impl_->ReleaseFeature();
  impl_->invalid_feature = false;
  impl_->parameters->Set(NVSDK_NGX_Parameter_CreationNodeMask, 1U);
  impl_->parameters->Set(NVSDK_NGX_Parameter_VisibilityNodeMask, 1U);
  impl_->parameters->Set(NVSDK_NGX_Parameter_Width, configuration.output_extent.width);
  impl_->parameters->Set(NVSDK_NGX_Parameter_Height, configuration.output_extent.height);
  impl_->parameters->Set(NVSDK_NGX_DLSSG_Parameter_BackbufferFormat, static_cast<unsigned int>(format));
  // The pinned Vulkan helper omits these documented fields; supply them
  // explicitly, as the SDK's D3D12 helper does, then use the device-specific API.
  impl_->parameters->Set(NVSDK_NGX_DLSSG_Parameter_InternalWidth, configuration.render_extent.width);
  impl_->parameters->Set(NVSDK_NGX_DLSSG_Parameter_InternalHeight, configuration.render_extent.height);
  impl_->parameters->Set(NVSDK_NGX_DLSSG_Parameter_DynamicResolution, 0U);
  if (!CreateDlssFeature(*impl_->device, NVSDK_NGX_Feature_FrameGeneration, impl_->parameters,
                         &impl_->feature, impl_->status)) return false;
  impl_->configured = true;
  impl_->failed_configuration = false;
  impl_->status = "DLSS 2x frame generation configured";
  REXLOG_INFO("DLSS FG {}: {}x{} input, {}x{} output", kDlssSdkVersion,
              configuration.render_extent.width, configuration.render_extent.height,
              configuration.output_extent.width, configuration.output_extent.height);
  return true;
#else
  return false;
#endif
}

bool DlssFrameGenerator::Encode(VkCommandBuffer command_buffer, const FrameInput& frame,
                                 const DlssFrameGenerationInput& input) {
  impl_->generated = false;
  if (!impl_->available || !impl_->configured || !command_buffer) return false;
  if (impl_->pending) {
    impl_->status = "DLSS FG previous encoded frame has no submission result";
    return false;
  }
  if (impl_->invalid_feature || impl_->failed_configuration) return false;
  if (!ValidDlssFrameGenerationInput(frame, input, impl_->format) ||
      frame.render_extent != impl_->configuration.render_extent ||
      frame.output_extent != impl_->configuration.output_extent) {
    impl_->history = false;
    impl_->status = "DLSS FG requires complete motion, depth, camera transforms, and HUD-free present color";
    return false;
  }
  if (impl_->history && frame.epoch == impl_->last_epoch && frame.sequence <= impl_->last_sequence) {
    impl_->status = "DLSS FG cannot evaluate a repeated or out-of-order scene";
    return false;
  }
#if defined(REX_HAS_DLSS_NGX)
  std::lock_guard lock(DlssSdkMutex());
  const bool reset = frame.reset || !impl_->history || frame.epoch != impl_->last_epoch ||
                     frame.sequence - impl_->last_sequence != 1;
  auto backbuffer = NgxImage(input.present_color, false);
  auto depth = NgxImage(frame.depth, false);
  auto motion = NgxImage(frame.motion, false);
  auto generated = NgxImage(input.generated_output, true);
  auto real = NgxImage(input.retained_real_output, true);
  NVSDK_NGX_VK_DLSSG_Eval_Params resources{};
  resources.pBackbuffer = &backbuffer;
  resources.pHudless = &backbuffer;
  resources.pDepth = &depth;
  resources.pMVecs = &motion;
  resources.pOutputInterpFrame = &generated;
  if (input.retained_real_output) resources.pOutputRealFrame = &real;
  NVSDK_NGX_DLSSG_Opt_Eval_Params options{};
  std::memcpy(options.cameraViewToClip, input.camera_view_to_clip.data(), sizeof(options.cameraViewToClip));
  std::memcpy(options.clipToCameraView, input.clip_to_camera_view.data(), sizeof(options.clipToCameraView));
  std::memcpy(options.clipToPrevClip, input.clip_to_previous_clip.data(), sizeof(options.clipToPrevClip));
  std::memcpy(options.prevClipToClip, input.previous_clip_to_clip.data(), sizeof(options.prevClipToClip));
  for (size_t i = 0; i < 4; ++i) options.clipToLensClip[i][i] = 1.0f;
  std::copy(input.camera_position.begin(), input.camera_position.end(), options.cameraPos);
  std::copy(input.camera_up.begin(), input.camera_up.end(), options.cameraUp);
  std::copy(input.camera_right.begin(), input.camera_right.end(), options.cameraRight);
  std::copy(input.camera_forward.begin(), input.camera_forward.end(), options.cameraFwd);
  options.jitterOffset[0] = input.projection_jitter[0];
  options.jitterOffset[1] = input.projection_jitter[1];
  // The guide's required-input section and parameter reference define pixels;
  // the generic struct's normalization comment is stale in the pinned SDK.
  options.mvecScale[0] = 1.0f;
  options.mvecScale[1] = 1.0f;
  options.cameraNear = frame.camera_near;
  options.cameraFar = frame.camera_far;
  options.cameraFOV = frame.vertical_fov_radians;
  options.cameraAspectRatio = input.camera_aspect_ratio;
  options.colorBuffersHDR = impl_->hdr;
  options.depthInverted = impl_->configuration.reversed_depth;
  options.cameraMotionIncluded = true;
  options.reset = reset;
  options.motionVectorsInvalidValue = std::numeric_limits<float>::max();
  options.multiFrameCount = 1;
  options.multiFrameIndex = 1;
  options.mvecsSubrectSize = {frame.render_extent.width, frame.render_extent.height};
  options.depthSubrectSize = options.mvecsSubrectSize;
  options.backbufferSubrectSize = {frame.output_extent.width, frame.output_extent.height};
  options.hudLessSubrectSize = options.backbufferSubrectSize;
  options.outputInterpSubrectSize = options.backbufferSubrectSize;
  if (input.retained_real_output) options.outputRealSubrectSize = options.backbufferSubrectSize;
  const auto result = NGX_VK_EVALUATE_DLSSG(command_buffer, impl_->feature, impl_->parameters,
                                          &resources, &options);
  if (NVSDK_NGX_FAILED(result)) {
    impl_->status = DlssResultMessage("DLSS frame-generation evaluation", result);
    impl_->history = false;
    impl_->failed_configuration = true;
    return false;
  }
  impl_->pending = true;
  impl_->pending_sequence = frame.sequence;
  impl_->pending_epoch = frame.epoch;
  impl_->generated = !reset;
  impl_->status = reset ? "DLSS FG history seeded" : "DLSS generated frame encoded";
  return true;
#else
  return false;
#endif
}

void DlssFrameGenerator::OnSubmitted(uint64_t sequence) {
  if (!impl_->pending || sequence != impl_->pending_sequence) {
    impl_->history = false;
    impl_->generated = false;
    impl_->invalid_feature = true;
    impl_->status = "DLSS FG submission identity did not match its encoded scene";
    impl_->pending = false;
    return;
  }
  impl_->last_sequence = impl_->pending_sequence;
  impl_->last_epoch = impl_->pending_epoch;
  impl_->history = true;
  impl_->pending = false;
}

void DlssFrameGenerator::OnDiscarded() {
  impl_->invalid_feature |= impl_->pending;
  impl_->history = false;
  impl_->generated = false;
  impl_->pending = false;
}

void DlssFrameGenerator::Shutdown() {
#if defined(REX_HAS_DLSS_NGX)
  if (impl_->initialized) {
    bool device_lost = false;
    if (impl_->feature && !WaitDlssDeviceIdle(*impl_->device, &device_lost) && !device_lost) {
      rex::FatalError("DLSS frame-generation shutdown cannot establish GPU completion; refusing unsafe resource and device destruction");
    }
    std::lock_guard lock(DlssSdkMutex());
    impl_->ReleaseFeature();
    if (impl_->parameters) NVSDK_NGX_VULKAN_DestroyParameters(impl_->parameters);
    ReleaseDlssDevice(impl_->device->device());
  }
#endif
  impl_ = std::make_unique<Impl>();
}

}  // namespace rex::graphics::gta4_native::temporal
