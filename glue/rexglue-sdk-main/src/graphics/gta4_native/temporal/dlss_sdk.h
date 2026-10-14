#pragma once

#include "dlss_bootstrap.h"

#if defined(REX_HAS_DLSS_NGX)
#include <filesystem>
#include <memory>

#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_vk.h>

namespace rex::ui::vulkan { class VulkanDevice; }

namespace rex::graphics::gta4_native::temporal {

// Owns every string referenced by the SDK's discovery/common-info structures.
// Nonmovable because the C structs contain pointers into this object.
struct DlssDiscovery {
  explicit DlssDiscovery(DlssFeature feature = DlssFeature::kSuperResolution);
  DlssDiscovery(const DlssDiscovery&) = delete;
  DlssDiscovery& operator=(const DlssDiscovery&) = delete;

  std::wstring data_path;
  std::wstring runtime_path;
  const wchar_t* runtime_paths[1]{};
  NVSDK_NGX_FeatureCommonInfo common{};
  NVSDK_NGX_FeatureDiscoveryInfo info{};
  std::string error;
};

std::string DlssResultMessage(const char* operation, NVSDK_NGX_Result result);

// Caller holds DlssSdkMutex for Acquire/Release/Create. Wait is called outside
// that lock, before feature destruction/reconfiguration.
bool AcquireDlssDevice(const ui::vulkan::VulkanDevice& device,
                       const std::shared_ptr<DlssDiscovery>& discovery,
                       std::string& status);
void ReleaseDlssDevice(VkDevice device);
bool WaitDlssDeviceIdle(const ui::vulkan::VulkanDevice& device, bool* device_lost = nullptr);
bool CreateDlssFeature(const ui::vulkan::VulkanDevice& device, NVSDK_NGX_Feature feature_id,
                       NVSDK_NGX_Parameter* parameters, NVSDK_NGX_Handle** handle,
                       std::string& status);

}  // namespace rex::graphics::gta4_native::temporal
#endif
