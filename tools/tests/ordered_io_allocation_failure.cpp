// Allocation-failure and reentrant-release tests for the production queue.
// Run as a separate process: a regressed lock ordering is bounded by the runner.
#include <rex/system/ordered_io_queue.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <new>
#include <thread>
#include <vector>

namespace { thread_local bool reject_allocations = false; }
void* operator new(std::size_t size) {
  if (reject_allocations) throw std::bad_alloc();
  if (void* result = std::malloc(size ? size : 1)) return result;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

int main() {
  using Queue = rex::system::OrderedIoQueue;
  Queue queue(4, 4096);
  std::atomic<unsigned> released{0}, executed{0}, published{0};
  unsigned accepted = 0;
  bool allocation_failed = false;
  for (unsigned index = 0; index < 4096; ++index) {
    auto owner = std::shared_ptr<unsigned>(new unsigned(index), [&](unsigned* value) {
      // Must not run with the queue lock held, including its bad_alloc path.
      (void)queue.statistics();
      delete value;
      released.fetch_add(1);
    });
    std::function<void()> work = [held = std::move(owner), &executed] { executed.fetch_add(1); };
    // Admit normal work first, then force the next queue-storage allocation to fail.
    reject_allocations = index != 0;
    const auto result = queue.Admit(16, std::move(work), [&] { published.fetch_add(1); });
    reject_allocations = false;
    if (result == Queue::Admission::kNoMemory) {
      allocation_failed = true;
      break;
    }
    if (result != Queue::Admission::kAccepted) {
      std::fprintf(stderr, "Unexpected admission status before allocation failure\n");
      return 1;
    }
    ++accepted;
  }
  if (!allocation_failed || released != 1 || published != accepted) {
    std::fprintf(stderr, "Failed admission retained ownership or published pending state\n");
    return 1;
  }
  std::vector<std::thread> workers;
  for (std::size_t lane = 0; lane < queue.lane_count(); ++lane)
    workers.emplace_back([&, lane] { queue.RunLane(lane); });
  queue.Close();
  queue.Drain();
  for (auto& worker : workers) worker.join();
  if (executed != accepted || released != accepted + 1 || published != accepted) {
    std::fprintf(stderr, "Accepted work or retained ownership was lost\n");
    return 1;
  }
  const auto statistics = queue.statistics();
  if (statistics.active || statistics.queued || statistics.accepted != statistics.completed) {
    std::fprintf(stderr, "Queue failed to drain\n");
    return 1;
  }
  std::printf("PASS allocation failure: %u accepted callbacks executed/released; rejected ownership released outside lock\n", accepted);
  return 0;
}
