#include "dlss_upscaler.h"

#include <limits>
#include <unordered_map>
#include <vector>

#include <rex/assert.h>
#include <rex/logging.h>
#include <rex/ui/vulkan/device.h>

#include "dlss_sdk.h"

namespace rex::graphics::gta4_native::temporal {
namespace {
#if defined(REX_HAS_DLSS_NGX)
NVSDK_NGX_PerfQuality_Value NgxQuality(Quality quality) {
  switch (quality) {
    case Quality::kNative: return NVSDK_NGX_PerfQuality_Value_DLAA;
    case Quality::kQuality: return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    case Quality::kBalanced: return NVSDK_NGX_PerfQuality_Value_Balanced;
    case Quality::kPerformance: return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    case Quality::kUltraPerformance: return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
  }
  return NVSDK_NGX_PerfQuality_Value_MaxQuality;
}

NVSDK_NGX_Resource_VK NgxImage(const Image& image, bool writable) {
  const VkImageSubresourceRange range{image.aspect, 0, 1, 0, 1};
  return NVSDK_NGX_Create_ImageView_Resource_VK(image.view, image.image, range, image.format,
                                              image.extent.width, image.extent.height, writable);
}
#endif
}  // namespace

bool ValidDlssSuperResolutionInput(const FrameInput& frame, bool hdr) {
  if (!ValidFrameInput(frame) || std::abs(frame.jitter_x) > 0.5f ||
      std::abs(frame.jitter_y) > 0.5f) return false;
  const auto color_format = [hdr](VkFormat format) {
    if (format == VK_FORMAT_R16G16B16A16_SFLOAT || format == VK_FORMAT_R32G32B32A32_SFLOAT ||
        format == VK_FORMAT_B10G11R11_UFLOAT_PACK32) return true;
    return !hdr && (format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_B8G8R8A8_UNORM);
  };
  const auto mask = [&](const Image& image) {
    return !image || (image.aspect == VK_IMAGE_ASPECT_COLOR_BIT &&
                       image.extent == frame.render_extent &&
                       (image.format == VK_FORMAT_R8_UNORM || image.format == VK_FORMAT_R16_SFLOAT ||
                        image.format == VK_FORMAT_R32_SFLOAT));
  };
  const bool depth = frame.depth.format == VK_FORMAT_D16_UNORM ||
                     frame.depth.format == VK_FORMAT_D24_UNORM_S8_UINT ||
                     frame.depth.format == VK_FORMAT_D32_SFLOAT ||
                     frame.depth.format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
                     frame.depth.format == VK_FORMAT_R32_SFLOAT;
  const VkImageAspectFlags depth_aspect = frame.depth.format == VK_FORMAT_R32_SFLOAT
      ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
  return color_format(frame.color.format) && color_format(frame.output.format) && depth &&
         frame.depth.aspect == depth_aspect && frame.color.aspect == VK_IMAGE_ASPECT_COLOR_BIT &&
         frame.output.aspect == VK_IMAGE_ASPECT_COLOR_BIT &&
         frame.motion.aspect == VK_IMAGE_ASPECT_COLOR_BIT &&
         (frame.motion.format == VK_FORMAT_R16G16_SFLOAT ||
          frame.motion.format == VK_FORMAT_R32G32_SFLOAT) && mask(frame.reactive) &&
         mask(frame.composition) &&
         (!frame.exposure || (frame.exposure.aspect == VK_IMAGE_ASPECT_COLOR_BIT &&
                              frame.exposure.extent == Extent{1, 1} &&
                              frame.exposure.format == VK_FORMAT_R32_SFLOAT));
}

struct DlssUpscaler::Impl {
  const ui::vulkan::VulkanDevice* device = nullptr;
  Configuration configuration{};
  std::string status = "DLSS has not been initialized";
  bool available = false;
  bool configured = false;
  bool failed_configuration = false;
  bool invalid_feature = false;
  bool pending = false;
  bool history = false;
  uint64_t pending_sequence = 0;
  uint64_t pending_epoch = 0;
  uint64_t last_sequence = 0;
  uint64_t last_epoch = 0;
#if defined(REX_HAS_DLSS_NGX)
  std::shared_ptr<DlssDiscovery> discovery;
  NVSDK_NGX_Parameter* parameters = nullptr;
  NVSDK_NGX_Handle* feature = nullptr;
  bool initialized = false;

  void ReleaseFeature() {
    if (feature) {
      const auto result = NVSDK_NGX_VULKAN_ReleaseFeature(feature);
      if (NVSDK_NGX_FAILED(result)) {
        REXLOG_WARN("{}", DlssResultMessage("DLSS release", result));
      }
      feature = nullptr;
    }
    configured = false;
    history = false;
  }
#endif
};

DlssUpscaler::DlssUpscaler() : impl_(std::make_unique<Impl>()) {}
DlssUpscaler::~DlssUpscaler() { Shutdown(); }
bool DlssUpscaler::available() const { return impl_->available; }
const std::string& DlssUpscaler::status() const { return impl_->status; }

bool DlssUpscaler::Initialize(const ui::vulkan::VulkanDevice& device) {
  if (impl_->device == &device) return impl_->available;
  Shutdown();
  impl_->device = &device;
  if (!device.dlss_extensions_enabled()) {
    impl_->status = device.dlss_unavailable_reason();
    return false;
  }
#if defined(REX_HAS_DLSS_NGX)
  std::lock_guard lock(DlssSdkMutex());
  impl_->discovery = std::make_shared<DlssDiscovery>();
  if (!impl_->discovery->error.empty()) {
    impl_->status = impl_->discovery->error;
    return false;
  }
  if (!AcquireDlssDevice(device, impl_->discovery, impl_->status)) return false;
  impl_->initialized = true;
  const auto result = NVSDK_NGX_VULKAN_GetCapabilityParameters(&impl_->parameters);
  if (NVSDK_NGX_FAILED(result) || !impl_->parameters) {
    impl_->status = DlssResultMessage("DLSS capability parameters", result);
    return false;
  }
  int supported = 0;
  int needs_driver = 0;
  impl_->parameters->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needs_driver);
  const auto available_result = impl_->parameters->Get(NVSDK_NGX_Parameter_SuperSampling_Available,
                                                      &supported);
  if (needs_driver) {
    unsigned int major = 0, minor = 0;
    impl_->parameters->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &major);
    impl_->parameters->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &minor);
    impl_->status = "DLSS requires NVIDIA driver " + std::to_string(major) + "." +
                    std::to_string(minor) + " or newer";
    return false;
  }
  if (NVSDK_NGX_FAILED(available_result) || !supported) {
    impl_->status = "NGX reports DLSS Super Resolution unavailable on the selected device";
    return false;
  }
  impl_->available = true;
  impl_->status = "DLSS Super Resolution ready (SDK " + std::string(kDlssSdkVersion) + ")";
  REXLOG_INFO("{}", impl_->status);
  return true;
#else
  impl_->status = "DLSS SDK is not included in this build";
  return false;
#endif
}

bool DlssUpscaler::QueryOptimalSettings(Extent output, Quality quality,
                                       DlssOptimalSettings& settings) {
  settings = {};
  if (!impl_->available || !output) return false;
#if defined(REX_HAS_DLSS_NGX)
  std::lock_guard lock(DlssSdkMutex());
  float unused_sharpness = 0.0f;
  const auto result = NGX_DLSS_GET_OPTIMAL_SETTINGS(
      impl_->parameters, output.width, output.height, NgxQuality(quality),
      &settings.render_extent.width, &settings.render_extent.height,
      &settings.maximum_extent.width, &settings.maximum_extent.height,
      &settings.minimum_extent.width, &settings.minimum_extent.height, &unused_sharpness);
  if (NVSDK_NGX_FAILED(result) || !settings.render_extent || !settings.minimum_extent ||
      !settings.maximum_extent) {
    impl_->status = DlssResultMessage("DLSS optimal-settings query", result);
    return false;
  }
  return true;
#else
  static_cast<void>(quality);
  return false;
#endif
}

bool DlssUpscaler::Configure(const Configuration& configuration) {
  if (!impl_->available || !configuration.render_extent || !configuration.output_extent) return false;
  if (impl_->pending) {
    impl_->status = "DLSS cannot reconfigure while an encoded frame awaits submission";
    return false;
  }
  if (impl_->configuration == configuration) {
    if (impl_->failed_configuration) return false;
    if (impl_->configured && !impl_->invalid_feature) return true;
  }
#if defined(REX_HAS_DLSS_NGX)
  DlssOptimalSettings settings;
  if (!QueryOptimalSettings(configuration.output_extent, configuration.quality, settings)) return false;
  const auto render = configuration.render_extent;
  if (render.width < settings.minimum_extent.width || render.height < settings.minimum_extent.height ||
      render.width > settings.maximum_extent.width || render.height > settings.maximum_extent.height) {
    impl_->status = "DLSS render extent is outside the SDK's supported quality range";
    return false;
  }
  impl_->configuration = configuration;
  impl_->failed_configuration = true;
  const auto& device = *impl_->device;
  if (!WaitDlssDeviceIdle(device)) {
    impl_->status = "Vulkan device did not retire DLSS work before reconfiguration";
    return false;
  }
  std::lock_guard lock(DlssSdkMutex());
  impl_->ReleaseFeature();
  impl_->invalid_feature = false;

  NVSDK_NGX_DLSS_Create_Params create{};
  create.Feature.InWidth = render.width;
  create.Feature.InHeight = render.height;
  create.Feature.InTargetWidth = configuration.output_extent.width;
  create.Feature.InTargetHeight = configuration.output_extent.height;
  create.Feature.InPerfQualityValue = NgxQuality(configuration.quality);
  create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
                                NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
  if (configuration.hdr) create.InFeatureCreateFlags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
  if (configuration.reversed_depth) {
    create.InFeatureCreateFlags |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
  }
  impl_->parameters->Set(NVSDK_NGX_Parameter_CreationNodeMask, 1U);
  impl_->parameters->Set(NVSDK_NGX_Parameter_VisibilityNodeMask, 1U);
  impl_->parameters->Set(NVSDK_NGX_Parameter_Width, create.Feature.InWidth);
  impl_->parameters->Set(NVSDK_NGX_Parameter_Height, create.Feature.InHeight);
  impl_->parameters->Set(NVSDK_NGX_Parameter_OutWidth, create.Feature.InTargetWidth);
  impl_->parameters->Set(NVSDK_NGX_Parameter_OutHeight, create.Feature.InTargetHeight);
  impl_->parameters->Set(NVSDK_NGX_Parameter_PerfQualityValue,
                         static_cast<int>(create.Feature.InPerfQualityValue));
  impl_->parameters->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, create.InFeatureCreateFlags);
  impl_->parameters->Set(NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects, 0);
  if (!CreateDlssFeature(device, NVSDK_NGX_Feature_SuperSampling, impl_->parameters,
                         &impl_->feature, impl_->status)) return false;
  impl_->configured = true;
  impl_->failed_configuration = false;
  impl_->status = "DLSS Super Resolution configured";
  REXLOG_INFO("DLSS {}: {}x{} -> {}x{}", kDlssSdkVersion, render.width, render.height,
              configuration.output_extent.width, configuration.output_extent.height);
  return true;
#else
  return false;
#endif
}

bool DlssUpscaler::Encode(VkCommandBuffer command_buffer, const FrameInput& frame) {
  if (!impl_->available || !impl_->configured || !command_buffer) return false;
  if (impl_->pending) {
    impl_->status = "DLSS previous encoded frame has no submission result";
    return false;
  }
  if (impl_->invalid_feature || impl_->failed_configuration) {
    impl_->status = "DLSS feature requires reconfiguration after a discarded or failed dispatch";
    return false;
  }
  if (!ValidDlssSuperResolutionInput(frame, impl_->configuration.hdr) ||
      frame.render_extent != impl_->configuration.render_extent ||
      frame.output_extent != impl_->configuration.output_extent) {
    impl_->history = false;
    impl_->status = "DLSS requires complete scene color, matching depth/object motion, and valid metadata";
    return false;
  }
  if (impl_->history && frame.epoch == impl_->last_epoch && frame.sequence <= impl_->last_sequence) {
    impl_->status = "DLSS cannot evaluate a repeated or out-of-order scene";
    return false;
  }
#if defined(REX_HAS_DLSS_NGX)
  std::lock_guard lock(DlssSdkMutex());
  auto color = NgxImage(frame.color, false);
  auto depth = NgxImage(frame.depth, false);
  auto motion = NgxImage(frame.motion, false);
  auto output = NgxImage(frame.output, true);
  auto reactive = NgxImage(frame.reactive, false);
  NVSDK_NGX_VK_DLSS_Eval_Params evaluate{};
  evaluate.Feature.pInColor = &color;
  evaluate.Feature.pInOutput = &output;
  evaluate.pInDepth = &depth;
  evaluate.pInMotionVectors = &motion;
  evaluate.InRenderSubrectDimensions = {frame.render_extent.width, frame.render_extent.height};
  evaluate.InJitterOffsetX = frame.jitter_x;
  evaluate.InJitterOffsetY = frame.jitter_y;
  evaluate.InMVScaleX = 1.0f;
  evaluate.InMVScaleY = 1.0f;
  evaluate.InReset = frame.reset || !impl_->history || frame.epoch != impl_->last_epoch ||
                    frame.sequence - impl_->last_sequence != 1;
  evaluate.InPreExposure = frame.pre_exposure;
  evaluate.InExposureScale = 1.0f;
  evaluate.InFrameTimeDeltaInMsec = frame.frame_time_ms;
  // DLSS's current-color bias accepts the reactive mask. FSR's composition mask
  // is not the same semantic input; the NGX transparency slot is reserved.
  if (frame.reactive) evaluate.pInBiasCurrentColorMask = &reactive;
  // The feature consistently uses NGX auto-exposure; do not mix a supplied
  // title adaptation texture with the auto-exposure create flag.
  const auto result = NGX_VULKAN_EVALUATE_DLSS_EXT(command_buffer, impl_->feature,
                                                  impl_->parameters, &evaluate);
  if (NVSDK_NGX_FAILED(result)) {
    impl_->status = DlssResultMessage("DLSS evaluation", result);
    impl_->history = false;
    impl_->failed_configuration = true;
    return false;
  }
  impl_->pending = true;
  impl_->pending_sequence = frame.sequence;
  impl_->pending_epoch = frame.epoch;
  impl_->status = "DLSS Super Resolution encoded";
  return true;
#else
  return false;
#endif
}

void DlssUpscaler::OnSubmitted(uint64_t sequence) {
  if (!impl_->pending || sequence != impl_->pending_sequence) {
    impl_->history = false;
    impl_->invalid_feature = true;
    impl_->status = "DLSS submission identity did not match its encoded scene";
    impl_->pending = false;
    return;
  }
  impl_->last_sequence = impl_->pending_sequence;
  impl_->last_epoch = impl_->pending_epoch;
  impl_->history = true;
  impl_->pending = false;
}

void DlssUpscaler::OnDiscarded() {
  // NGX may advance CPU-side history during EvaluateFeature. Reset alone cannot
  // roll that bookkeeping back when the command buffer was never submitted.
  impl_->invalid_feature |= impl_->pending;
  impl_->history = false;
  impl_->pending = false;
}

void DlssUpscaler::Shutdown() {
#if defined(REX_HAS_DLSS_NGX)
  if (impl_->initialized) {
    // The renderer owns the device and destroys this adapter first. As with all
    // renderer shutdown, no future submission may reference these resources.
    bool device_lost = false;
    if (impl_->feature && !WaitDlssDeviceIdle(*impl_->device, &device_lost) && !device_lost) {
      rex::FatalError("DLSS shutdown cannot establish GPU completion; refusing unsafe resource and device destruction");
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
