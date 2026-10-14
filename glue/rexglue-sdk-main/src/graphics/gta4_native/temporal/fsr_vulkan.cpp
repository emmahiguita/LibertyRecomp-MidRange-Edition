#include "fsr_vulkan.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <utility>

#include <rex/ui/vulkan/device.h>

#if defined(REX_HAS_FIDELITYFX_VULKAN) && REX_HAS_FIDELITYFX_VULKAN
#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_framegeneration.h>
#include <ffx_api/ffx_upscale.h>
#include <ffx_api/vk/ffx_api_vk.h>
#define REX_FSR_VK 1
#else
#define REX_FSR_VK 0
#endif

namespace rex::graphics::gta4_native::temporal {

bool FsrSupportsDevice(const ui::vulkan::VulkanDevice& device,
                       std::string* reason, bool frame_generation) {
  const auto reject = [&](const char* message) {
    if (reason) *reason = message;
    return false;
  };
#if !REX_FSR_VK
  return reject("FidelityFX Vulkan support was not built");
#else
#if !defined(REX_HAS_FIDELITYFX_FRAMEGENERATION) || !REX_HAS_FIDELITYFX_FRAMEGENERATION
  if (frame_generation) return reject("FidelityFX frame generation was not built");
#endif
  const auto& features = device.properties();
  if (!features.shaderStorageImageExtendedFormats ||
      !features.shaderStorageImageWriteWithoutFormat)
    return reject("FidelityFX requires enabled extended storage-image formats and unformatted writes");
  if (frame_generation && !features.shaderStorageImageReadWithoutFormat)
    return reject("FidelityFX interpolation requires enabled unformatted storage-image reads");
  // The backend selects FP16 from physical support. Native VulkanDevice enables
  // supported float16, and these selected shaders also declare Int16 capability.
  if (features.shaderFloat16 && !features.shaderInt16)
    return reject("FidelityFX FP16 permutations require enabled shaderInt16");
  // At eee08db1688ac3d1275a70b728f4a8ba22914213, GetDeviceCapabilitiesVK
  // advertises shader model 5.1. All three components require 6.6 to force
  // wave64, so those permutations and half subgroup operations are not selected.
  if (reason) reason->clear();
  return true;
#endif
}

namespace {

struct History {
  uint64_t sequence = 0;
  uint64_t epoch = 0;
  uint64_t time_ns = 0;
  uint64_t pending_sequence = 0;
  uint64_t pending_epoch = 0;
  uint64_t pending_time_ns = 0;
  bool valid = false;
  bool pending = false;

  bool NeedsReset(const FrameInput& frame) const {
    return frame.reset || !valid || frame.epoch != epoch || frame.sequence <= sequence ||
           frame.sequence - sequence != 1 || (time_ns && frame.time_ns <= time_ns);
  }
  void Record(const FrameInput& frame) {
    pending_sequence = frame.sequence;
    pending_epoch = frame.epoch;
    pending_time_ns = frame.time_ns;
    pending = true;
  }
  void Commit() {
    if (!pending) return;
    sequence = pending_sequence;
    epoch = pending_epoch;
    time_ns = pending_time_ns;
    valid = true;
    pending = false;
  }
  void Discard() {
    valid = false;
    pending = false;
  }
};

bool ValidConfiguration(const Configuration& configuration) {
  return configuration.render_extent && configuration.output_extent &&
         configuration.output_extent.width <= uint32_t(std::numeric_limits<int32_t>::max()) &&
         configuration.output_extent.height <= uint32_t(std::numeric_limits<int32_t>::max()) &&
         configuration.render_extent.width <= configuration.output_extent.width &&
         configuration.render_extent.height <= configuration.output_extent.height;
}

bool ValidFormats(const FrameInput& frame) {
  const auto mask = [&](const Image& image) {
    return !image.image ||
           (image.extent == frame.render_extent && image.aspect == VK_IMAGE_ASPECT_COLOR_BIT &&
            (image.format == VK_FORMAT_R8_UNORM || image.format == VK_FORMAT_R16_SFLOAT ||
             image.format == VK_FORMAT_R32_SFLOAT));
  };
  return frame.color.aspect == VK_IMAGE_ASPECT_COLOR_BIT &&
         frame.output.aspect == VK_IMAGE_ASPECT_COLOR_BIT &&
         ((frame.depth.format == VK_FORMAT_R32_SFLOAT &&
           frame.depth.aspect == VK_IMAGE_ASPECT_COLOR_BIT) ||
          (frame.depth.format == VK_FORMAT_D32_SFLOAT &&
           frame.depth.aspect == VK_IMAGE_ASPECT_DEPTH_BIT)) &&
         frame.motion.aspect == VK_IMAGE_ASPECT_COLOR_BIT &&
         (frame.motion.format == VK_FORMAT_R16G16_SFLOAT ||
          frame.motion.format == VK_FORMAT_R32G32_SFLOAT) &&
         mask(frame.reactive) && mask(frame.composition) &&
         (!frame.exposure.image ||
          (frame.exposure.extent == Extent{1, 1} &&
           frame.exposure.aspect == VK_IMAGE_ASPECT_COLOR_BIT &&
           (frame.exposure.format == VK_FORMAT_R16_SFLOAT ||
            frame.exposure.format == VK_FORMAT_R32_SFLOAT)));
}

bool ValidCamera(const FsrFrameGenerationInput& input) {
  const auto finite = [](const float (&vector)[3]) {
    return std::all_of(std::begin(vector), std::end(vector),
                       [](float value) { return std::isfinite(value); });
  };
  const auto dot = [](const float (&a)[3], const float (&b)[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  };
  const auto unit = [&](const float (&vector)[3]) {
    return finite(vector) && std::abs(dot(vector, vector) - 1.0f) <= 0.01f;
  };
  return finite(input.camera_position) && unit(input.camera_up) && unit(input.camera_right) &&
         unit(input.camera_forward) &&
         std::abs(dot(input.camera_up, input.camera_right)) <= 0.01f &&
         std::abs(dot(input.camera_up, input.camera_forward)) <= 0.01f &&
         std::abs(dot(input.camera_right, input.camera_forward)) <= 0.01f &&
         std::isfinite(input.view_space_to_meters) && input.view_space_to_meters > 0.0f &&
         std::isfinite(input.minimum_luminance) && input.minimum_luminance >= 0.0f &&
         std::isfinite(input.maximum_luminance) &&
         input.maximum_luminance > input.minimum_luminance;
}

#if REX_FSR_VK
struct ResolverEntry {
  PFN_vkGetDeviceProcAddr resolver = nullptr;
  size_t users = 0;
};
std::mutex resolver_mutex;
std::unordered_map<VkDevice, ResolverEntry> resolvers;
// The pinned SDK temporarily stores VkDeviceContext in a global during creation.
std::mutex create_mutex;

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL ResolveFsrFunction(VkDevice device, const char* name) {
  PFN_vkGetDeviceProcAddr resolver = nullptr;
  {
    std::lock_guard lock(resolver_mutex);
    const auto found = resolvers.find(device);
    if (found == resolvers.end()) return nullptr;
    resolver = found->second.resolver;
  }
  if (const auto function = resolver(device, name)) return function;
  // Core Vulkan 1.1 functions need not expose the KHR names when the extension
  // was promoted and its extension name was not enabled on the logical device.
  for (const auto& alias : std::array{
           std::pair{"vkGetBufferMemoryRequirements2KHR", "vkGetBufferMemoryRequirements2"},
           std::pair{"vkGetImageMemoryRequirements2KHR", "vkGetImageMemoryRequirements2"},
           std::pair{"vkBindBufferMemory2KHR", "vkBindBufferMemory2"},
           std::pair{"vkBindImageMemory2KHR", "vkBindImageMemory2"}}) {
    if (std::strcmp(name, alias.first) == 0) return resolver(device, alias.second);
  }
  return nullptr;
}

void FsrMessage(uint32_t type, const wchar_t* message) {
  std::fwprintf(stderr, L"FidelityFX %ls: %ls\n",
                type == FFX_API_MESSAGE_TYPE_ERROR ? L"error" : L"warning", message);
}

uint32_t QualityMode(Quality quality) {
  switch (quality) {
    case Quality::kNative: return FFX_UPSCALE_QUALITY_MODE_NATIVEAA;
    case Quality::kQuality: return FFX_UPSCALE_QUALITY_MODE_QUALITY;
    case Quality::kBalanced: return FFX_UPSCALE_QUALITY_MODE_BALANCED;
    case Quality::kPerformance: return FFX_UPSCALE_QUALITY_MODE_PERFORMANCE;
    case Quality::kUltraPerformance: return FFX_UPSCALE_QUALITY_MODE_ULTRA_PERFORMANCE;
  }
  return FFX_UPSCALE_QUALITY_MODE_QUALITY;
}

FfxApiResource Wrap(const Image& image, bool output = false) {
  if (!image.image) return {};
  VkImageCreateInfo description{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  description.imageType = VK_IMAGE_TYPE_2D;
  description.format = image.format;
  description.extent = {image.extent.width, image.extent.height, 1};
  description.mipLevels = 1;
  description.arrayLayers = 1;
  description.samples = VK_SAMPLE_COUNT_1_BIT;
  description.tiling = VK_IMAGE_TILING_OPTIMAL;
  description.usage = image.usage;
  description.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  auto resource = ffxApiGetImageResourceDescriptionVK(image.image, description, 0);
  // FFX GENERIC_READ maps to GENERAL + SHADER_READ in this pinned Vulkan backend.
  // COMPUTE_READ maps to SHADER_READ_ONLY_OPTIMAL and would misdeclare our layout.
  return ffxApiGetResourceVK(reinterpret_cast<void*>(image.image), resource,
                            output ? FFX_API_RESOURCE_STATE_UNORDERED_ACCESS
                                   : FFX_API_RESOURCE_STATE_GENERIC_READ);
}

bool KnownFormat(const Image& image) {
  return !image.image || ffxApiGetSurfaceFormatVK(image.format) != FFX_API_SURFACE_FORMAT_UNKNOWN;
}

struct Context {
  ffxContext context = nullptr;
  // Keep the linked create descriptors alive for the context's full lifetime,
  // as required by ffxCreateContext's public API contract.
  ffxCreateBackendVKDesc backend{};
  Configuration configuration{};
  History history;
  std::string provider;
  std::string error;
  bool initialized = false;
  bool failed_configuration = false;
  bool requires_recreation = false;
  PFN_vkGetDeviceProcAddr original_resolver = nullptr;

  bool Fail(const char* operation, ffxReturnCode_t result) {
    error = std::string(operation) + " failed (FidelityFX code " + std::to_string(result) + ")";
    history.Discard();
    return false;
  }
  bool Initialize(VkPhysicalDevice physical, VkDevice device, PFN_vkGetDeviceProcAddr resolver) {
    if (!physical || !device || !resolver) {
      error = "FidelityFX requires a valid Vulkan device and function resolver";
      return false;
    }
    if (initialized) {
      if (backend.vkPhysicalDevice == physical && backend.vkDevice == device &&
          original_resolver == resolver) return true;
      error = "Shutdown the previous FidelityFX device after GPU completion before rebinding";
      return false;
    }
    backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
    backend.vkPhysicalDevice = physical;
    backend.vkDevice = device;
    {
      std::lock_guard lock(resolver_mutex);
      auto& entry = resolvers[device];
      if (entry.users && entry.resolver != resolver) {
        error = "Conflicting FidelityFX Vulkan function resolvers for the same device";
        return false;
      }
      entry.resolver = resolver;
      ++entry.users;
    }
    original_resolver = resolver;
    backend.vkDeviceProcAddr = ResolveFsrFunction;
    initialized = true;
    error.clear();
    return true;
  }
  void Destroy() {
    if (context) {
      const auto result = ffxDestroyContext(&context, nullptr);
      if (result != FFX_API_RETURN_OK) Fail("Destroy context", result);
      context = nullptr;
    }
    history.Discard();
    provider.clear();
    requires_recreation = false;
  }
  void Shutdown() {
    Destroy();
    if (initialized) {
      std::lock_guard lock(resolver_mutex);
      const auto found = resolvers.find(backend.vkDevice);
      if (found != resolvers.end() && --found->second.users == 0) resolvers.erase(found);
    }
    initialized = false;
    failed_configuration = false;
    original_resolver = nullptr;
  }
  void ReadProvider() {
    ffxQueryGetProviderVersion version{};
    version.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
    if (ffxQuery(&context, &version.header) == FFX_API_RETURN_OK && version.versionName) {
      provider = version.versionName;
    }
  }
  bool Validate(VkCommandBuffer command, const FrameInput& frame) {
    if (!context || !command || history.pending || requires_recreation) {
      error = "FidelityFX needs a configured context, command buffer, and resolved prior submission";
      return false;
    }
    if (!ValidFrameInput(frame) || !ValidFormats(frame) ||
        frame.render_extent != configuration.render_extent ||
        frame.output_extent != configuration.output_extent || !KnownFormat(frame.color) ||
        !KnownFormat(frame.output)) {
      error = "FidelityFX requires matching scene color, depth, complete object motion and extents";
      history.Discard();
      return false;
    }
    if (history.valid && frame.epoch == history.epoch && frame.sequence == history.sequence) {
      error = "Repeated presentation cannot advance FidelityFX scene history";
      return false;
    }
    return true;
  }
};
#endif

}  // namespace

struct FsrUpscalerVulkan::Impl {
#if REX_FSR_VK
  Context state;
  ffxCreateContextDescUpscale create{};
#else
  std::string provider;
  std::string error = "FidelityFX Vulkan runtime was not built";
#endif
};

FsrUpscalerVulkan::FsrUpscalerVulkan() : impl_(std::make_unique<Impl>()) {}
FsrUpscalerVulkan::~FsrUpscalerVulkan() { Shutdown(); }

bool FsrUpscalerVulkan::Initialize(VkPhysicalDevice physical, VkDevice device,
                                  PFN_vkGetDeviceProcAddr resolver) {
#if REX_FSR_VK
  return impl_->state.Initialize(physical, device, resolver);
#else
  return false;
#endif
}

bool FsrUpscalerVulkan::QueryRenderExtent(Extent output, Quality quality, Extent& render) {
  render = {};
#if REX_FSR_VK
  if (!output) return false;
  ffxQueryDescUpscaleGetRenderResolutionFromQualityMode query{};
  query.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETRENDERRESOLUTIONFROMQUALITYMODE;
  query.displayWidth = output.width;
  query.displayHeight = output.height;
  query.qualityMode = QualityMode(quality);
  query.pOutRenderWidth = &render.width;
  query.pOutRenderHeight = &render.height;
  const auto result = ffxQuery(nullptr, &query.header);
  if (result != FFX_API_RETURN_OK) return impl_->state.Fail("Query render extent", result);
  return bool(render);
#else
  return false;
#endif
}

bool FsrUpscalerVulkan::QueryJitter(uint64_t index, Extent render, Extent output, float& x, float& y) {
  x = y = 0.0f;
#if REX_FSR_VK
  if (!render || !output || render.width > output.width || render.height > output.height) return false;
  int32_t phase_count = 0;
  ffxQueryDescUpscaleGetJitterPhaseCount phase{};
  phase.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
  phase.renderWidth = render.width;
  phase.displayWidth = output.width;
  phase.pOutPhaseCount = &phase_count;
  auto result = ffxQuery(nullptr, &phase.header);
  if (result != FFX_API_RETURN_OK) return impl_->state.Fail("Query jitter phases", result);
  if (phase_count <= 0) return impl_->state.Fail("Invalid jitter phase count", FFX_API_RETURN_ERROR);
  ffxQueryDescUpscaleGetJitterOffset query{};
  query.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET;
  query.index = static_cast<int32_t>(index % static_cast<uint64_t>(phase_count));
  query.phaseCount = phase_count;
  query.pOutX = &x;
  query.pOutY = &y;
  result = ffxQuery(nullptr, &query.header);
  if (result != FFX_API_RETURN_OK) return impl_->state.Fail("Query jitter offset", result);
  return std::isfinite(x) && std::isfinite(y);
#else
  return false;
#endif
}

bool FsrUpscalerVulkan::Configure(const Configuration& configuration) {
#if REX_FSR_VK
  auto& state = impl_->state;
  if (!state.initialized || !ValidConfiguration(configuration) || state.history.pending) {
    state.error = "Invalid FidelityFX configuration or unresolved submission";
    return false;
  }
  if (state.configuration == configuration) {
    if (state.context && !state.requires_recreation) return true;
    if (state.failed_configuration) return false;
  }
  state.Destroy();
  state.configuration = configuration;
  state.failed_configuration = true;
  auto& create = impl_->create;
  create = {};
  create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
  create.header.pNext = &state.backend.header;
  create.flags = FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
  if (configuration.hdr) create.flags |= FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE;
  if (configuration.reversed_depth) create.flags |= FFX_UPSCALE_ENABLE_DEPTH_INVERTED;
  if (configuration.infinite_depth) create.flags |= FFX_UPSCALE_ENABLE_DEPTH_INFINITE;
  create.maxRenderSize = {configuration.render_extent.width, configuration.render_extent.height};
  create.maxUpscaleSize = {configuration.output_extent.width, configuration.output_extent.height};
  create.fpMessage = FsrMessage;
  ffxReturnCode_t result;
  {
    std::lock_guard lock(create_mutex);
    result = ffxCreateContext(&state.context, &create.header, nullptr);
  }
  if (result != FFX_API_RETURN_OK) {
    state.Destroy();
    return state.Fail("Create upscaler", result);
  }
  state.failed_configuration = false;
  state.ReadProvider();
  state.error.clear();
  return true;
#else
  return false;
#endif
}

bool FsrUpscalerVulkan::Encode(VkCommandBuffer command, const FrameInput& frame,
                              bool sharpening, float sharpness) {
#if REX_FSR_VK
  auto& state = impl_->state;
  if (!state.Validate(command, frame)) return false;
  if (!std::isfinite(sharpness) || sharpness < 0.0f || sharpness > 1.0f) {
    state.error = "FidelityFX sharpness must be in [0, 1]";
    state.history.Discard();
    return false;
  }
  ffxDispatchDescUpscale dispatch{};
  dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
  dispatch.commandList = command;
  dispatch.color = Wrap(frame.color);
  dispatch.depth = Wrap(frame.depth);
  dispatch.motionVectors = Wrap(frame.motion);
  dispatch.exposure = Wrap(frame.exposure);
  dispatch.reactive = Wrap(frame.reactive);
  dispatch.transparencyAndComposition = Wrap(frame.composition);
  dispatch.output = Wrap(frame.output, true);
  dispatch.jitterOffset = {frame.jitter_x, frame.jitter_y};
  dispatch.motionVectorScale = {1.0f, 1.0f};
  dispatch.renderSize = {frame.render_extent.width, frame.render_extent.height};
  dispatch.upscaleSize = {frame.output_extent.width, frame.output_extent.height};
  dispatch.enableSharpening = sharpening;
  dispatch.sharpness = sharpness;
  dispatch.frameTimeDelta = frame.frame_time_ms;
  dispatch.preExposure = frame.pre_exposure;
  dispatch.reset = state.history.NeedsReset(frame);
  dispatch.cameraNear = frame.camera_near;
  dispatch.cameraFar = frame.camera_far;
  dispatch.cameraFovAngleVertical = frame.vertical_fov_radians;
  dispatch.viewSpaceToMetersFactor = 1.0f;
  const auto result = ffxDispatch(&state.context, &dispatch.header);
  if (result != FFX_API_RETURN_OK) {
    state.requires_recreation = true;
    return state.Fail("Dispatch upscaler", result);
  }
  state.history.Record(frame);
  state.error.clear();
  return true;
#else
  return false;
#endif
}

void FsrUpscalerVulkan::OnSubmitted() {
#if REX_FSR_VK
  impl_->state.history.Commit();
#endif
}
void FsrUpscalerVulkan::OnDiscarded() {
#if REX_FSR_VK
  impl_->state.requires_recreation |= impl_->state.history.pending;
  impl_->state.history.Discard();
#endif
}
void FsrUpscalerVulkan::Reset() {
#if REX_FSR_VK
  if (impl_->state.history.pending) OnDiscarded();
  else impl_->state.history.Discard();
#endif
}
void FsrUpscalerVulkan::Shutdown() {
#if REX_FSR_VK
  impl_->state.Shutdown();
#endif
}
bool FsrUpscalerVulkan::available() const {
#if REX_FSR_VK
  return impl_->state.context != nullptr;
#else
  return false;
#endif
}
const std::string& FsrUpscalerVulkan::provider_name() const {
#if REX_FSR_VK
  return impl_->state.provider;
#else
  return impl_->provider;
#endif
}
const std::string& FsrUpscalerVulkan::last_error() const {
#if REX_FSR_VK
  return impl_->state.error;
#else
  return impl_->error;
#endif
}

struct FsrFrameGenerationVulkan::Impl {
#if REX_FSR_VK && defined(REX_HAS_FIDELITYFX_FRAMEGENERATION) && REX_HAS_FIDELITYFX_FRAMEGENERATION
  Context state;
  ffxCreateContextDescFrameGeneration create{};
  VkFormat format = VK_FORMAT_UNDEFINED;
  bool generated = false;
#else
  std::string provider;
  std::string error = "FidelityFX Vulkan frame-generation provider was not built";
#endif
};

#if REX_FSR_VK && defined(REX_HAS_FIDELITYFX_FRAMEGENERATION) && REX_HAS_FIDELITYFX_FRAMEGENERATION
#define REX_FSR_FG 1
#else
#define REX_FSR_FG 0
#endif

FsrFrameGenerationVulkan::FsrFrameGenerationVulkan() : impl_(std::make_unique<Impl>()) {}
FsrFrameGenerationVulkan::~FsrFrameGenerationVulkan() { Shutdown(); }
bool FsrFrameGenerationVulkan::Initialize(VkPhysicalDevice physical, VkDevice device,
                                         PFN_vkGetDeviceProcAddr resolver) {
#if REX_FSR_FG
  return impl_->state.Initialize(physical, device, resolver);
#else
  return false;
#endif
}
bool FsrFrameGenerationVulkan::Configure(const Configuration& configuration, VkFormat format) {
#if REX_FSR_FG
  auto& state = impl_->state;
  const uint32_t ffx_format = ffxApiGetSurfaceFormatVK(format);
  if (!state.initialized || !ValidConfiguration(configuration) || state.history.pending ||
      ffx_format == FFX_API_SURFACE_FORMAT_UNKNOWN) {
    state.error = "Invalid FidelityFX frame-generation configuration or unresolved submission";
    return false;
  }
  if (state.configuration == configuration && impl_->format == format) {
    if (state.context && !state.requires_recreation) return true;
    if (state.failed_configuration) return false;
  }
  state.Destroy();
  state.configuration = configuration;
  state.failed_configuration = true;
  impl_->format = format;
  impl_->generated = false;
  auto& create = impl_->create;
  create = {};
  create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
  create.header.pNext = &state.backend.header;
  create.flags = FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING;
  if (configuration.hdr) create.flags |= FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE;
  if (configuration.reversed_depth) create.flags |= FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED;
  if (configuration.infinite_depth) create.flags |= FFX_FRAMEGENERATION_ENABLE_DEPTH_INFINITE;
  create.maxRenderSize = {configuration.render_extent.width, configuration.render_extent.height};
  create.displaySize = {configuration.output_extent.width, configuration.output_extent.height};
  create.backBufferFormat = ffx_format;
  ffxReturnCode_t result;
  {
    std::lock_guard lock(create_mutex);
    result = ffxCreateContext(&state.context, &create.header, nullptr);
  }
  if (result != FFX_API_RETURN_OK) {
    state.Destroy();
    return state.Fail("Create frame generation", result);
  }
  state.failed_configuration = false;
  state.ReadProvider();
  state.error.clear();
  return true;
#else
  return false;
#endif
}
bool FsrFrameGenerationVulkan::Encode(VkCommandBuffer command, const FrameInput& frame,
                                     const FsrFrameGenerationInput& presentation) {
#if REX_FSR_FG
  auto& state = impl_->state;
  impl_->generated = false;
  if (!state.Validate(command, frame)) return false;
  const auto& color = presentation.present_color;
  const auto& output = presentation.generated_output;
  if (!ValidCamera(presentation) || !color || !output || color.image == output.image ||
      color.format != impl_->format || output.format != impl_->format ||
      color.extent != frame.output_extent || output.extent != frame.output_extent ||
      color.layout != VK_IMAGE_LAYOUT_GENERAL || output.layout != VK_IMAGE_LAYOUT_GENERAL ||
      color.aspect != VK_IMAGE_ASPECT_COLOR_BIT || output.aspect != VK_IMAGE_ASPECT_COLOR_BIT ||
      !(color.usage & VK_IMAGE_USAGE_SAMPLED_BIT) || !(output.usage & VK_IMAGE_USAGE_STORAGE_BIT) ||
      (state.configuration.hdr !=
       (presentation.transfer_function != FsrTransferFunction::kSrgb))) {
    state.error = "FidelityFX frame generation requires display scene color, distinct output, valid camera and transfer metadata";
    state.history.Discard();
    return false;
  }
  const bool reset = state.history.NeedsReset(frame);
  ffxConfigureDescFrameGeneration configure{};
  configure.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
  configure.frameGenerationEnabled = true;
  configure.allowAsyncWorkloads = false;
  // presentColor already excludes the HUD, which is composited after SDK work.
  // HUDLessColor is an optional DIFFERENT image for extracting UI from a final
  // backbuffer. Registering the same VkImage in both roles makes the pinned
  // backend track two independent layouts and emit conflicting barriers.
  configure.HUDLessColor = {};
  configure.flags = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;
  configure.generationRect = {0, 0, static_cast<int32_t>(frame.output_extent.width),
                              static_cast<int32_t>(frame.output_extent.height)};
  configure.frameID = frame.sequence;
  auto result = ffxConfigure(&state.context, &configure.header);
  if (result != FFX_API_RETURN_OK) return state.Fail("Configure frame generation", result);

  ffxDispatchDescFrameGenerationPrepareCameraInfo camera{};
  camera.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO;
  std::copy(std::begin(presentation.camera_position), std::end(presentation.camera_position), camera.cameraPosition);
  std::copy(std::begin(presentation.camera_up), std::end(presentation.camera_up), camera.cameraUp);
  std::copy(std::begin(presentation.camera_right), std::end(presentation.camera_right), camera.cameraRight);
  std::copy(std::begin(presentation.camera_forward), std::end(presentation.camera_forward), camera.cameraForward);
  ffxDispatchDescFrameGenerationPrepare prepare{};
  prepare.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;
  prepare.header.pNext = &camera.header;
  prepare.commandList = command;
  prepare.frameID = frame.sequence;
  prepare.flags = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;
  prepare.renderSize = {frame.render_extent.width, frame.render_extent.height};
  prepare.jitterOffset = {frame.jitter_x, frame.jitter_y};
  prepare.motionVectorScale = {1.0f, 1.0f};
  prepare.frameTimeDelta = frame.frame_time_ms;
  prepare.unused_reset = reset;
  prepare.cameraNear = frame.camera_near;
  prepare.cameraFar = frame.camera_far;
  prepare.cameraFovAngleVertical = frame.vertical_fov_radians;
  prepare.viewSpaceToMetersFactor = presentation.view_space_to_meters;
  prepare.depth = Wrap(frame.depth);
  prepare.motionVectors = Wrap(frame.motion);
  result = ffxDispatch(&state.context, &prepare.header);
  if (result != FFX_API_RETURN_OK) {
    state.requires_recreation = true;
    return state.Fail("Prepare frame generation", result);
  }

  ffxDispatchDescFrameGeneration generate{};
  generate.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
  generate.commandList = command;
  generate.presentColor = Wrap(color);
  generate.outputs[0] = Wrap(output, true);
  generate.numGeneratedFrames = 1;
  generate.reset = reset;
  switch (presentation.transfer_function) {
    case FsrTransferFunction::kSrgb:
      generate.backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
      break;
    case FsrTransferFunction::kPq:
      generate.backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_PQ;
      break;
    case FsrTransferFunction::kScRgb:
      generate.backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SCRGB;
      break;
  }
  generate.minMaxLuminance[0] = presentation.minimum_luminance;
  generate.minMaxLuminance[1] = presentation.maximum_luminance;
  generate.generationRect = configure.generationRect;
  generate.frameID = frame.sequence;
  result = ffxDispatch(&state.context, &generate.header);
  if (result != FFX_API_RETURN_OK) {
    state.requires_recreation = true;
    return state.Fail("Dispatch frame generation", result);
  }
  state.history.Record(frame);
  impl_->generated = !reset;
  state.error.clear();
  return true;
#else
  return false;
#endif
}
void FsrFrameGenerationVulkan::OnSubmitted() {
#if REX_FSR_FG
  impl_->state.history.Commit();
#endif
}
void FsrFrameGenerationVulkan::OnDiscarded() {
#if REX_FSR_FG
  impl_->state.requires_recreation |= impl_->state.history.pending;
  impl_->state.history.Discard();
  impl_->generated = false;
#endif
}
void FsrFrameGenerationVulkan::Reset() {
#if REX_FSR_FG
  if (impl_->state.history.pending) OnDiscarded();
  else impl_->state.history.Discard();
  impl_->generated = false;
#endif
}
void FsrFrameGenerationVulkan::Shutdown() {
#if REX_FSR_FG
  impl_->state.Shutdown();
  impl_->generated = false;
#endif
}
bool FsrFrameGenerationVulkan::available() const {
#if REX_FSR_FG
  return impl_->state.context != nullptr;
#else
  return false;
#endif
}
bool FsrFrameGenerationVulkan::has_generated_frame() const {
#if REX_FSR_FG
  return impl_->generated;
#else
  return false;
#endif
}
const std::string& FsrFrameGenerationVulkan::provider_name() const {
#if REX_FSR_FG
  return impl_->state.provider;
#else
  return impl_->provider;
#endif
}
const std::string& FsrFrameGenerationVulkan::last_error() const {
#if REX_FSR_FG
  return impl_->state.error;
#else
  return impl_->error;
#endif
}

}  // namespace rex::graphics::gta4_native::temporal
