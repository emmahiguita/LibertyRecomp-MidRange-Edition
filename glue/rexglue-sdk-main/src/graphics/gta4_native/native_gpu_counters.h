#pragma once
// TEMP (Android perf investigation): whole-frame GPU hardware counters through
// VK_KHR_performance_query. One single-pass counter set, one query per frame
// slot, averaged over the reporting interval.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <rex/ui/vulkan/device.h>
#include <rex/ui/vulkan/instance.h>

namespace rex::graphics::gta4_native {

class NativeGpuCounters {
 public:
  static constexpr uint32_t kSlots = 2;

  // Returns false (and stays inert) when the driver offers no usable counters.
  bool Initialize(const ui::vulkan::VulkanDevice* device, std::string* log) {
    device_ = device;
    auto fail = [&](const char* reason) { if (log) *log = std::string("FAIL:") + reason; return false; };
    if (!device) return fail("no-device");
    if (!device->performance_query_enabled()) return fail("extension-or-feature-missing");
    const auto& ifn = device->vulkan_instance()->functions();
    const VkInstance instance = device->vulkan_instance()->instance();
    enumerate_ = reinterpret_cast<PFN_vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR>(
        ifn.vkGetInstanceProcAddr(instance,
                                  "vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR"));
    passes_ = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR>(
        ifn.vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR"));
    acquire_ = reinterpret_cast<PFN_vkAcquireProfilingLockKHR>(
        ifn.vkGetDeviceProcAddr(device->device(), "vkAcquireProfilingLockKHR"));
    if (!enumerate_ || !passes_ || !acquire_) return fail("entry-points");
    const VkPhysicalDevice physical = device->physical_device();
    family_ = device->queue_family_graphics_compute();
    uint32_t count = 0;
    if (enumerate_(physical, family_, &count, nullptr, nullptr) != VK_SUCCESS || !count) return fail("no-counters");
    counters_.assign(count, {VK_STRUCTURE_TYPE_PERFORMANCE_COUNTER_KHR});
    descriptions_.assign(count, {VK_STRUCTURE_TYPE_PERFORMANCE_COUNTER_DESCRIPTION_KHR});
    if (enumerate_(physical, family_, &count, counters_.data(), descriptions_.data()) != VK_SUCCESS)
      return false;
    if (log) {
      for (uint32_t i = 0; i < count; ++i) *log += fmt::format("{}|", descriptions_[i].name);
    }
    // Every block's busy-cycle counter plus the reference clock, kept while the
    // set still needs a single pass.
    std::vector<uint32_t> wanted;
    for (uint32_t i = 0; i < count; ++i) {
      const std::string name = descriptions_[i].name;
      if (name.find("ALWAYS_COUNT") != std::string::npos) wanted.insert(wanted.begin(), i);
    }
    for (uint32_t i = 0; i < count; ++i) {
      const std::string name = descriptions_[i].name;
      if (name.find("BUSY_CYCLES") != std::string::npos ||
          name.find("STALL_CYCLES") != std::string::npos ||
          name.find("WORKING_CYCLES") != std::string::npos ||
          name.find("NON_EXECUTION") != std::string::npos ||
          name.find("STARVE") != std::string::npos)
        wanted.push_back(i);
    }
    for (uint32_t index : wanted) {
      selected_.push_back(index);
      if (PassCount() != 1) selected_.pop_back();
    }
    if (selected_.empty()) return fail("no-single-pass-set");
    VkQueryPoolPerformanceCreateInfoKHR performance_info{
        VK_STRUCTURE_TYPE_QUERY_POOL_PERFORMANCE_CREATE_INFO_KHR};
    performance_info.queueFamilyIndex = family_;
    performance_info.counterIndexCount = uint32_t(selected_.size());
    performance_info.pCounterIndices = selected_.data();
    VkQueryPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    pool_info.pNext = &performance_info;
    pool_info.queryType = VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR;
    pool_info.queryCount = 1;
    const auto& dfn = device->functions();
    for (auto& pool : pools_) {
      if (dfn.vkCreateQueryPool(device->device(), &pool_info, nullptr, &pool) != VK_SUCCESS) return fail("create-pool");
    }
    VkAcquireProfilingLockInfoKHR lock_info{VK_STRUCTURE_TYPE_ACQUIRE_PROFILING_LOCK_INFO_KHR};
    lock_info.timeout = UINT64_MAX;
    if (acquire_(device->device(), &lock_info) != VK_SUCCESS) return fail("profiling-lock");
    sums_.assign(selected_.size(), 0.0);
    ready_ = true;
    return true;
  }

  bool ready() const { return ready_; }

  void BeginFrame(VkCommandBuffer command_buffer, uint32_t slot) {
    if (!ready_ || slot >= kSlots) return;
    const auto& dfn = device_->functions();
    if (pending_[slot]) {
      // The slot's previous submission completed before its command buffer
      // could be reused, so its result is available.
      std::vector<VkPerformanceCounterResultKHR> results(selected_.size());
      if (dfn.vkGetQueryPoolResults(device_->device(), pools_[slot], 0, 1,
                                    results.size() * sizeof(results[0]), results.data(),
                                    sizeof(results[0]) * results.size(), 0) == VK_SUCCESS) {
        for (size_t i = 0; i < selected_.size(); ++i) sums_[i] += Value(selected_[i], results[i]);
        ++frames_;
      }
      pending_[slot] = false;
    }
    dfn.vkCmdResetQueryPool(command_buffer, pools_[slot], 0, 1);
    dfn.vkCmdBeginQuery(command_buffer, pools_[slot], 0, 0);
    active_[slot] = true;
  }

  void EndFrame(VkCommandBuffer command_buffer, uint32_t slot) {
    if (!ready_ || slot >= kSlots || !active_[slot]) return;
    device_->functions().vkCmdEndQuery(command_buffer, pools_[slot], 0);
    active_[slot] = false;
    pending_[slot] = true;
  }

  // Averages per frame since the previous report, busy counters as a share of
  // the reference clock.
  std::string Report() {
    if (!ready_ || !frames_) return {};
    double reference = 0.0;
    for (size_t i = 0; i < selected_.size(); ++i)
      if (std::string(descriptions_[selected_[i]].name).find("ALWAYS_COUNT") != std::string::npos)
        reference = std::max(reference, sums_[i]);
    std::string out = fmt::format("frames={}", frames_);
    for (size_t i = 0; i < selected_.size(); ++i) {
      const std::string name = descriptions_[selected_[i]].name;
      if (reference > 0 && name.find("ALWAYS_COUNT") == std::string::npos &&
          counters_[selected_[i]].unit == VK_PERFORMANCE_COUNTER_UNIT_CYCLES_KHR) {
        out += fmt::format(" {}={:.0f}%", name, 100.0 * sums_[i] / reference);
      } else {
        out += fmt::format(" {}={:.3g}", name, sums_[i] / frames_);
      }
    }
    std::fill(sums_.begin(), sums_.end(), 0.0);
    frames_ = 0;
    return out;
  }

 private:
  uint32_t PassCount() {
    VkQueryPoolPerformanceCreateInfoKHR info{VK_STRUCTURE_TYPE_QUERY_POOL_PERFORMANCE_CREATE_INFO_KHR};
    info.queueFamilyIndex = family_;
    info.counterIndexCount = uint32_t(selected_.size());
    info.pCounterIndices = selected_.data();
    uint32_t passes = 0;
    passes_(device_->physical_device(), &info, &passes);
    return passes;
  }
  double Value(uint32_t counter, const VkPerformanceCounterResultKHR& result) const {
    switch (counters_[counter].storage) {
      case VK_PERFORMANCE_COUNTER_STORAGE_INT32_KHR: return result.int32;
      case VK_PERFORMANCE_COUNTER_STORAGE_INT64_KHR: return double(result.int64);
      case VK_PERFORMANCE_COUNTER_STORAGE_UINT32_KHR: return result.uint32;
      case VK_PERFORMANCE_COUNTER_STORAGE_UINT64_KHR: return double(result.uint64);
      case VK_PERFORMANCE_COUNTER_STORAGE_FLOAT32_KHR: return result.float32;
      case VK_PERFORMANCE_COUNTER_STORAGE_FLOAT64_KHR: return result.float64;
      default: return 0.0;
    }
  }

  const ui::vulkan::VulkanDevice* device_ = nullptr;
  PFN_vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR enumerate_ = nullptr;
  PFN_vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR passes_ = nullptr;
  PFN_vkAcquireProfilingLockKHR acquire_ = nullptr;
  uint32_t family_ = 0;
  std::vector<VkPerformanceCounterKHR> counters_;
  std::vector<VkPerformanceCounterDescriptionKHR> descriptions_;
  std::vector<uint32_t> selected_;
  std::array<VkQueryPool, kSlots> pools_{};
  std::array<bool, kSlots> pending_{};
  std::array<bool, kSlots> active_{};
  std::vector<double> sums_;
  uint32_t frames_ = 0;
  bool ready_ = false;
};

}  // namespace rex::graphics::gta4_native
