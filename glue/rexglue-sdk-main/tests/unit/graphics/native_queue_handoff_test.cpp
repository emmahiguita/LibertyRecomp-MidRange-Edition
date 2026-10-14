#include <catch2/catch_test_macros.hpp>
#include <rex/ui/vulkan/image_access.h>
#include <rex/ui/vulkan/native_queue_policy.h>
#include <array>
#include <atomic>
#include <cstring>
#include <thread>
#include <vector>
using namespace rex::ui::vulkan;
namespace { VkSemaphore Sem(uintptr_t value) { return (VkSemaphore)value; } }

TEST_CASE("Native queue policy retains a complete single queue fallback", "[native-queue]") {
  std::array<VkQueueFamilyProperties, 4> families{};
  for (auto& family : families) { family.queueCount = 1; family.queueFlags = 7; }
  CHECK(SelectNativeOffscreenQueueFamily(families, 0, true, true, true) == 1);
  CHECK(SelectNativeOffscreenQueueFamily(families, 1, true, true, true) == 0);
  for (int mask = 0; mask < 7; ++mask)
    CHECK(SelectNativeOffscreenQueueFamily(families, 0, mask & 1, mask & 2, mask & 4) == 0);
  families[1].queueCount = 0; families[2].queueFlags = VK_QUEUE_TRANSFER_BIT;
  CHECK(SelectNativeOffscreenQueueFamily(families, 0, true, true, true) == 3);
  families[3].queueFlags = VK_QUEUE_GRAPHICS_BIT;
  CHECK(SelectNativeOffscreenQueueFamily(families, 0, true, true, true) == 0);
  CHECK(SelectNativeOffscreenQueueFamily({}, UINT32_MAX, true, true, true) == UINT32_MAX);
}
TEST_CASE("Shared image access commits only submitted work", "[native-queue]") {
  ImageAccessTimeline timeline; timeline.Initialize(Sem(1));
  { auto abandoned = timeline.Acquire(); CHECK(abandoned.ticket().wait_value == 0);
    CHECK(abandoned.ticket().signal_value == 1); }
  CHECK(timeline.submitted_value() == 0);
  { auto first = timeline.Acquire(); CHECK(first.Commit()); CHECK_FALSE(first.Commit()); }
  CHECK(timeline.submitted_value() == 1);
  { auto read = timeline.Acquire(); CHECK(read.ticket().wait_value == 1);
    CHECK(read.ticket().signal_value == 2); auto moved = std::move(read); CHECK(moved.Commit()); }
  { auto next_write = timeline.Acquire(); CHECK(next_write.ticket().wait_value == 2);
    CHECK(next_write.ticket().signal_value == 3); CHECK(next_write.Commit()); }
  CHECK(timeline.submitted_value() == 3);
}
TEST_CASE("Shared image submit retains binary WSI semaphores and extension chain", "[native-queue]") {
  VkSemaphore acquire = Sem(1), present = Sem(2);
  VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkDeviceGroupSubmitInfo existing{}; existing.sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_SUBMIT_INFO;
  VkSubmitInfo submit{}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; submit.pNext = &existing;
  submit.waitSemaphoreCount = submit.signalSemaphoreCount = 1;
  submit.pWaitSemaphores = &acquire; submit.pWaitDstStageMask = &stage;
  submit.pSignalSemaphores = &present;
  ImageAccessSubmit access; REQUIRE(access.Attach(submit, {Sem(3), 4, 5}));
  REQUIRE(submit.waitSemaphoreCount == 2); REQUIRE(submit.signalSemaphoreCount == 2);
  CHECK(submit.pWaitSemaphores[0] == acquire); CHECK(submit.pSignalSemaphores[0] == present);
  CHECK(submit.pWaitSemaphores[1] == Sem(3)); CHECK(submit.pSignalSemaphores[1] == Sem(3));
  CHECK(submit.pWaitDstStageMask[0] == stage);
  CHECK(submit.pWaitDstStageMask[1] == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
  const auto* values = static_cast<const VkTimelineSemaphoreSubmitInfo*>(submit.pNext);
  CHECK(values->pNext == &existing); CHECK(values->waitSemaphoreValueCount == 2);
  CHECK(values->signalSemaphoreValueCount == 2); CHECK(values->pWaitSemaphoreValues[0] == 0);
  CHECK(values->pSignalSemaphoreValues[0] == 0); CHECK(values->pWaitSemaphoreValues[1] == 4);
  CHECK(values->pSignalSemaphoreValues[1] == 5);
  ImageAccessSubmit twice; CHECK_FALSE(twice.Attach(submit, {Sem(4), 0, 1}));
}
TEST_CASE("Disabled access is unchanged and invalid access is never published", "[native-queue]") {
  VkSubmitInfo submit{}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  const auto before = submit; ImageAccessSubmit access;
  CHECK(access.Attach(submit, {})); CHECK(std::memcmp(&before, &submit, sizeof(submit)) == 0);
  CHECK_FALSE(access.Attach(submit, {Sem(1), UINT64_MAX, 0}));
  CHECK_FALSE(access.Attach(submit, {Sem(1), 2, 2}));
  submit.waitSemaphoreCount = 1;
  CHECK_FALSE(access.Attach(submit, {Sem(1), 0, 1}));
  ImageAccessTimeline disabled; auto lease = disabled.Acquire();
  CHECK(lease.ticket().valid()); CHECK_FALSE(lease.ticket().semaphore); CHECK(lease.Commit());
}
TEST_CASE("Access ordering remains transactional across concurrent writers readers and aborts", "[native-queue]") {
  ImageAccessTimeline timeline; timeline.Initialize(Sem(7));
  constexpr unsigned workers = 6, iterations = 5000;
  std::atomic<unsigned> committed{0}; std::atomic<bool> valid{true};
  std::vector<std::thread> threads;
  for (unsigned worker = 0; worker < workers; ++worker) threads.emplace_back([&,worker]{
    for (unsigned i = 0; i < iterations; ++i) {
      if ((i + worker) % 7 == 0) { auto aborted = timeline.Acquire(); continue; }
      auto lease = timeline.Acquire();
      if (lease.ticket().wait_value != committed.load() ||
          lease.ticket().signal_value != committed.load() + 1) valid = false;
      ++committed;
      if (!lease.Commit()) valid = false;
    }
  });
  for (auto& thread : threads) thread.join();
  CHECK(valid.load()); CHECK(timeline.submitted_value() == committed.load());
  auto blocked_image = timeline.Acquire();
  ImageAccessTimeline independent; independent.Initialize(Sem(8));
  auto other = independent.Acquire(); CHECK(other.Commit());
  CHECK(independent.submitted_value() == 1);
}
