#pragma once
// TEMP (Android perf investigation): driver shader statistics for the
// pipelines that issue the most draws (VK_KHR_pipeline_executable_properties).
#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include <rex/ui/vulkan/device.h>
#include <rex/ui/vulkan/instance.h>

namespace rex::graphics::gta4_native {

class NativePipelineStats {
 public:
  bool Initialize(const ui::vulkan::VulkanDevice* device) {
    device_ = device;
    if (!device || !device->pipeline_statistics_enabled()) return false;
    const auto& ifn = device->vulkan_instance()->functions();
    properties_ = reinterpret_cast<PFN_vkGetPipelineExecutablePropertiesKHR>(
        ifn.vkGetDeviceProcAddr(device->device(), "vkGetPipelineExecutablePropertiesKHR"));
    statistics_ = reinterpret_cast<PFN_vkGetPipelineExecutableStatisticsKHR>(
        ifn.vkGetDeviceProcAddr(device->device(), "vkGetPipelineExecutableStatisticsKHR"));
    ready_ = properties_ && statistics_;
    return ready_;
  }
  bool ready() const { return ready_; }

  void Observe(VkPipeline pipeline) {
    if (!ready_ || !pipeline) return;
    Entry& entry = entries_[pipeline];
    if (!entry.queried) {
      entry.queried = true;
      entry.summary = Describe(pipeline);
    }
    ++entry.draws;
    ++total_draws_;
  }

  std::string Report(size_t top) {
    std::vector<std::pair<uint64_t, const Entry*>> sorted;
    for (const auto& [pipeline, entry] : entries_)
      if (entry.draws) sorted.emplace_back(entry.draws, &entry);
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    std::string out = fmt::format("draws={} pipelines={}", total_draws_, sorted.size());
    for (size_t i = 0; i < sorted.size() && i < top; ++i)
      out += fmt::format("\n#{} {:.1f}% {}", i, 100.0 * sorted[i].first / std::max<uint64_t>(1, total_draws_),
                         sorted[i].second->summary);
    for (auto& [pipeline, entry] : entries_) entry.draws = 0;
    total_draws_ = 0;
    return out;
  }

 private:
  struct Entry {
    bool queried = false;
    uint64_t draws = 0;
    std::string summary;
  };
  std::string Describe(VkPipeline pipeline) {
    VkPipelineInfoKHR info{VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR};
    info.pipeline = pipeline;
    uint32_t count = 0;
    if (properties_(device_->device(), &info, &count, nullptr) != VK_SUCCESS || !count)
      return "no-executables";
    std::vector<VkPipelineExecutablePropertiesKHR> executables(
        count, {VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR});
    properties_(device_->device(), &info, &count, executables.data());
    std::string out;
    for (uint32_t index = 0; index < count; ++index) {
      VkPipelineExecutableInfoKHR executable{VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR};
      executable.pipeline = pipeline;
      executable.executableIndex = index;
      uint32_t statistic_count = 0;
      statistics_(device_->device(), &executable, &statistic_count, nullptr);
      std::vector<VkPipelineExecutableStatisticKHR> statistics(
          statistic_count, {VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR});
      statistics_(device_->device(), &executable, &statistic_count, statistics.data());
      out += fmt::format(" [{}:", executables[index].name);
      for (const auto& statistic : statistics) {
        const std::string name = statistic.name;
        // Keep the numbers that decide throughput on Adreno.
        if (name.find("Wave") == std::string::npos && name.find("Instruction Count") == std::string::npos &&
            name.find("register") == std::string::npos && name.find("Register") == std::string::npos &&
            name.find("cycles") == std::string::npos && name.find("Cycles") == std::string::npos &&
            name.find("Preamble") == std::string::npos && name.find("NOP") == std::string::npos &&
            name.find("sync") == std::string::npos)
          continue;
        switch (statistic.format) {
          case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR:
            out += fmt::format(" {}={}", name, statistic.value.b32 ? 1 : 0); break;
          case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR:
            out += fmt::format(" {}={}", name, statistic.value.i64); break;
          case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR:
            out += fmt::format(" {}={}", name, statistic.value.u64); break;
          case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR:
            out += fmt::format(" {}={:.3g}", name, statistic.value.f64); break;
          default: break;
        }
      }
      out += "]";
    }
    return out;
  }

  const ui::vulkan::VulkanDevice* device_ = nullptr;
  PFN_vkGetPipelineExecutablePropertiesKHR properties_ = nullptr;
  PFN_vkGetPipelineExecutableStatisticsKHR statistics_ = nullptr;
  std::unordered_map<VkPipeline, Entry> entries_;
  uint64_t total_draws_ = 0;
  bool ready_ = false;
};

}  // namespace rex::graphics::gta4_native
