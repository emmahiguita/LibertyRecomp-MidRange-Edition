#pragma once

#include <mutex>
#include <string>
#include <vector>

#include <rex/ui/vulkan/api.h>

namespace rex::graphics::gta4_native::temporal {

// Stable UUIDv5 of the project's canonical URL. This is a CUSTOM engine project
// identifier, not an NVIDIA-issued application ID.
inline constexpr char kDlssProjectId[] = "0bea89be-74ae-5670-9eeb-ec2047dddab4";
inline constexpr char kDlssEngineVersion[] = "LibertyRecomp-1";
inline constexpr char kDlssSdkVersion[] = "310.9.1";

struct DlssExtensionRequirements {
  bool available = false;
  std::vector<std::string> extensions;
  std::string reason;
};

enum class DlssFeature { kSuperResolution, kFrameGeneration };

// Called before vkCreateInstance / vkCreateDevice. Unavailable NGX never makes
// ordinary Vulkan device creation fail. Probe the selected physical device.
DlssExtensionRequirements QueryDlssInstanceExtensions(DlssFeature feature = DlssFeature::kSuperResolution);
DlssExtensionRequirements QueryDlssDeviceExtensions(VkInstance instance,
                                                  VkPhysicalDevice physical_device,
                                                  uint32_t vendor_id,
                                                  DlssFeature feature = DlssFeature::kSuperResolution);

// Every NGX entry point, including discovery and destruction, shares this mutex.
// These functions and the adapter are built into rexruntime exactly once.
std::mutex& DlssSdkMutex();

}  // namespace rex::graphics::gta4_native::temporal
