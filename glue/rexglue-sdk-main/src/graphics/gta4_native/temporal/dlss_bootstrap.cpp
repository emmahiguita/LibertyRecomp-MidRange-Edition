#include "dlss_bootstrap.h"

#include <limits>
#include <unordered_map>

#include <rex/assert.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/vulkan/device.h>

#include "dlss_sdk.h"
#if defined(REX_HAS_DLSS_NGX)
#define UTF_CPP_CPLUSPLUS 201703L  // The host build uses -fno-char8_t.
#include <utf8.h>
#endif

namespace rex::graphics::gta4_native::temporal {

std::mutex& DlssSdkMutex() {
  static std::mutex mutex;
  return mutex;
}

#if defined(REX_HAS_DLSS_NGX)
namespace {
struct DeviceUse {
  size_t users = 0;
  std::shared_ptr<DlssDiscovery> discovery;
};
std::unordered_map<VkDevice, DeviceUse> device_users;

std::wstring NgxPath(const std::filesystem::path& path) {
#if REX_PLATFORM_WIN32
  return path.native();
#else
  std::u32string decoded;
  const auto bytes = path.native();
  utf8::utf8to32(bytes.begin(), bytes.end(), std::back_inserter(decoded));
  return {decoded.begin(), decoded.end()};
#endif
}
void NVSDK_CONV LogNgx(const char* message, NVSDK_NGX_Logging_Level,
                      NVSDK_NGX_Feature) {
  if (message) REXLOG_DEBUG("DLSS/NGX: {}", message);
}

DlssExtensionRequirements ExtensionResult(const char* operation, NVSDK_NGX_Result result,
                                          uint32_t count, VkExtensionProperties* properties) {
  DlssExtensionRequirements requirements;
  if (NVSDK_NGX_FAILED(result)) {
    requirements.reason = DlssResultMessage(operation, result);
    return requirements;
  }
  if (count && !properties) {
    requirements.reason = "NGX returned an invalid extension list";
    return requirements;
  }
  requirements.available = true;
  for (uint32_t i = 0; i < count; ++i) {
    requirements.extensions.emplace_back(properties[i].extensionName);
  }
  return requirements;
}
}  // namespace

DlssDiscovery::DlssDiscovery(DlssFeature feature) {
  const auto directory = rex::filesystem::GetUserFolder() / "LibertyRecomp" / "ngx";
  std::error_code ec;
  std::filesystem::create_directories(directory, ec);
  if (ec) {
    error = "Could not create the NGX data directory: " + ec.message();
    return;
  }
  try {
    data_path = NgxPath(directory);
    runtime_path = NgxPath(rex::filesystem::GetExecutableFolder());
  } catch (const std::exception& e) {
    error = std::string("Could not encode NGX paths: ") + e.what();
    return;
  }
  runtime_paths[0] = runtime_path.c_str();
  common.PathListInfo.Path = runtime_paths;
  common.PathListInfo.Length = 1;
  common.LoggingInfo.LoggingCallback = &LogNgx;
  common.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON;
  common.LoggingInfo.DisableOtherLoggingSinks = true;
  info.SDKVersion = NVSDK_NGX_Version_API;
  info.FeatureID = feature == DlssFeature::kFrameGeneration ? NVSDK_NGX_Feature_FrameGeneration
                                                          : NVSDK_NGX_Feature_SuperSampling;
  info.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
  info.Identifier.v.ProjectDesc.ProjectId = kDlssProjectId;
  info.Identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
  info.Identifier.v.ProjectDesc.EngineVersion = kDlssEngineVersion;
  info.ApplicationDataPath = data_path.c_str();
  info.FeatureInfo = &common;
}

bool WaitDlssDeviceIdle(const ui::vulkan::VulkanDevice& device, bool* device_lost) {
  // vkDeviceWaitIdle requires external synchronization of every queue, including
  // the independent presentation queue. Acquire them in device/family order.
  std::vector<ui::vulkan::VulkanDevice::Queue::Acquisition> locks;
  for (const auto& family : device.queue_families()) {
    for (const auto& queue : family.queues) locks.emplace_back(queue->Acquire());
  }
  const auto result = device.functions().vkDeviceWaitIdle(device.device());
  if (device_lost) *device_lost = result == VK_ERROR_DEVICE_LOST;
  return result == VK_SUCCESS;
}

bool AcquireDlssDevice(const ui::vulkan::VulkanDevice& device,
                       const std::shared_ptr<DlssDiscovery>& discovery,
                       std::string& status) {
  auto found = device_users.find(device.device());
  if (found == device_users.end()) {
    const auto& instance = *device.vulkan_instance();
    const auto result = NVSDK_NGX_VULKAN_Init_with_ProjectID(
        kDlssProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, kDlssEngineVersion,
        discovery->data_path.c_str(), instance.instance(), device.physical_device(),
        device.device(), instance.functions().vkGetInstanceProcAddr,
        instance.functions().vkGetDeviceProcAddr, &discovery->common);
    if (NVSDK_NGX_FAILED(result)) {
      status = DlssResultMessage("DLSS initialization", result);
      return false;
    }
    found = device_users.emplace(device.device(), DeviceUse{0, discovery}).first;
  }
  ++found->second.users;
  return true;
}

void ReleaseDlssDevice(VkDevice device) {
  const auto found = device_users.find(device);
  if (found != device_users.end() && --found->second.users == 0) {
    NVSDK_NGX_VULKAN_Shutdown1(device);
    device_users.erase(found);
  }
}

bool CreateDlssFeature(const ui::vulkan::VulkanDevice& device, NVSDK_NGX_Feature feature_id,
                       NVSDK_NGX_Parameter* parameters, NVSDK_NGX_Handle** handle,
                       std::string& status) {
  const auto& fn = device.functions();
  VkCommandPool pool = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  const auto cleanup = [&] {
    if (fence) fn.vkDestroyFence(device.device(), fence, nullptr);
    if (pool) fn.vkDestroyCommandPool(device.device(), pool, nullptr);
  };
  VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  pool_info.queueFamilyIndex = device.queue_family_native_offscreen();
  if (fn.vkCreateCommandPool(device.device(), &pool_info, nullptr, &pool) != VK_SUCCESS) {
    status = "Could not allocate the DLSS initialization command pool";
    return false;
  }
  VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  allocate.commandPool = pool;
  allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocate.commandBufferCount = 1;
  VkCommandBuffer command_buffer = VK_NULL_HANDLE;
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  if (fn.vkAllocateCommandBuffers(device.device(), &allocate, &command_buffer) != VK_SUCCESS ||
      fn.vkBeginCommandBuffer(command_buffer, &begin) != VK_SUCCESS ||
      fn.vkCreateFence(device.device(), &fence_info, nullptr, &fence) != VK_SUCCESS) {
    status = "Could not begin DLSS feature initialization";
    cleanup();
    return false;
  }
  const auto result = NVSDK_NGX_VULKAN_CreateFeature1(device.device(), command_buffer,
                                                       feature_id, parameters, handle);
  if (NVSDK_NGX_FAILED(result) || !*handle) {
    status = DlssResultMessage("DLSS feature creation", result);
    if (*handle) NVSDK_NGX_VULKAN_ReleaseFeature(*handle);
    *handle = nullptr;
    cleanup();
    return false;
  }
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &command_buffer;
  bool submitted = false;
  if (fn.vkEndCommandBuffer(command_buffer) == VK_SUCCESS) {
    auto queue = device.AcquireQueue(device.queue_family_native_offscreen(), 0);
    submitted = fn.vkQueueSubmit(queue.queue(), 1, &submit, fence) == VK_SUCCESS;
  }
  bool completed = false;
  if (submitted) {
    const auto waited = fn.vkWaitForFences(device.device(), 1, &fence, VK_TRUE,
                                          std::numeric_limits<uint64_t>::max());
    completed = waited == VK_SUCCESS;
    if (!completed && waited != VK_ERROR_DEVICE_LOST) {
      bool device_lost = false;
      if (!WaitDlssDeviceIdle(device, &device_lost) && !device_lost) {
        rex::FatalError("DLSS feature creation cannot establish GPU completion; refusing unsafe feature and command-pool destruction");
      }
    }
  }
  if (!submitted || !completed) {
    status = "DLSS initialization command submission or completion failed";
    if (*handle) NVSDK_NGX_VULKAN_ReleaseFeature(*handle);
    *handle = nullptr;
    cleanup();
    return false;
  }
  cleanup();
  return true;
}

std::string DlssResultMessage(const char* operation, NVSDK_NGX_Result result) {
  return std::string(operation) + " failed (NGX " +
         std::to_string(static_cast<uint32_t>(result)) + ")";
}
#endif

DlssExtensionRequirements QueryDlssInstanceExtensions(DlssFeature feature) {
#if defined(REX_HAS_DLSS_NGX)
  std::lock_guard lock(DlssSdkMutex());
  DlssDiscovery discovery(feature);
  if (!discovery.error.empty()) return {false, {}, discovery.error};
  uint32_t count = 0;
  VkExtensionProperties* properties = nullptr;
  const auto result = NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(
      &discovery.info, &count, &properties);
  return ExtensionResult("DLSS instance extension query", result, count, properties);
#else
  return {false, {}, "DLSS SDK is not included in this build"};
#endif
}

DlssExtensionRequirements QueryDlssDeviceExtensions(VkInstance instance,
                                                  VkPhysicalDevice physical_device,
                                                  uint32_t vendor_id, DlssFeature feature) {
#if defined(REX_HAS_DLSS_NGX)
  if (vendor_id != 0x10DE) return {false, {}, "DLSS requires a compatible NVIDIA device"};
  if (!instance || !physical_device) return {false, {}, "DLSS device query has no Vulkan device"};
  std::lock_guard lock(DlssSdkMutex());
  DlssDiscovery discovery(feature);
  if (!discovery.error.empty()) return {false, {}, discovery.error};
  NVSDK_NGX_FeatureRequirement support{};
  const auto support_result = NVSDK_NGX_VULKAN_GetFeatureRequirements(
      instance, physical_device, &discovery.info, &support);
  if (NVSDK_NGX_FAILED(support_result)) {
    return {false, {}, DlssResultMessage("DLSS selected-device capability query", support_result)};
  }
  if (support.FeatureSupported != NVSDK_NGX_FeatureSupportResult_Supported) {
    return {false, {}, "DLSS selected device is unsupported (NGX reasons " +
                           std::to_string(static_cast<uint32_t>(support.FeatureSupported)) + ")"};
  }
  uint32_t count = 0;
  VkExtensionProperties* properties = nullptr;
  const auto result = NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(
      instance, physical_device, &discovery.info, &count, &properties);
  return ExtensionResult("DLSS device extension query", result, count, properties);
#else
  return {false, {}, "DLSS SDK is not included in this build"};
#endif
}

}  // namespace rex::graphics::gta4_native::temporal
