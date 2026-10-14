#include "../../../gta4-recomp/src/gta4_streaming_policy.h"
#include "../../../gta4-recomp/src/gta4_streaming_trace_policy.h"
#include <rex/system/ordered_io_queue.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <latch>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
size_t checks = 0;
void Check(bool condition, const char* expression, int line) {
  ++checks;
  if (!condition) throw std::runtime_error(std::string("line ") + std::to_string(line) + ": " + expression);
}
#define CHECK(expression) Check(bool(expression), #expression, __LINE__)
using namespace gta4::streaming;
using rex::system::OrderedIoQueue;
using Admission = OrderedIoQueue::Admission;

struct Workers {
  OrderedIoQueue& queue;
  std::vector<std::thread> threads;
  explicit Workers(OrderedIoQueue& q) : queue(q) {
    for (size_t lane = 0; lane < q.lane_count(); ++lane)
      threads.emplace_back([&, lane] { queue.RunLane(lane); });
  }
  ~Workers() {
    queue.Close();
    for (auto& thread : threads) thread.join();
  }
};

void PolicyTests() {
  CHECK(RequestedBudget(0, 4) == 0);
  CHECK(RequestedBudget(209715200, 1.5) == 314572800);
  CHECK(RequestedBudget(1234, -1) == 1234);
  CHECK(RequestedBudget(1234, NAN) == 1234);
  CHECK(RequestedBudget(UINT32_MAX, 4) == UINT32_MAX);
  CHECK(RequestedBudget(1234, INFINITY) == 1234);
  CHECK(BudgetWithHeadroom(100u, 150u, 16u) == 134u);
  CHECK(BudgetWithHeadroom(100u, 150u, 75u) == 100u);
  CHECK(BudgetWithHeadroom(100u, 80u, 16u) == 80u);
  CHECK(BudgetWithHeadroom(100u, 150u, 0u) == 150u);
  CHECK(BudgetWithHeadroom(0u, 0u, 16u) == 0u);
  CHECK(BudgetWithHeadroom(100u, 150u, 4294967295u) == 100u);
  CHECK(EntryIndex(0, 1024) == kInvalidEntry);
  CHECK(EntryIndex(1024, 1023) == kInvalidEntry);
  CHECK(EntryIndex(1024, 1024) == 0);
  CHECK(EntryIndex(1024, 1048) == 1);
  CHECK(EntryIndex(1024, 1049) == kInvalidEntry);
  CHECK(EntryIndex(1024, 1024 + 65535 * 24) == kInvalidEntry);
  CHECK(PreloadMargin(50, 100, 250, 40, .75) == 130);
  CHECK(PreloadMargin(50, 100, 250, 150, 2) == 250);
  CHECK(PreloadMargin(50, -100, 20, -40, -1) == 50);
  CHECK(PreloadMargin(50, NAN, 250, 40, .75) == 50);
  CHECK(PreloadMargin(600, 100, 250, 40, .75) == 600);
  CHECK(ReadinessDeadline(100, 50, 100, 20, false) == 100);
  CHECK(ReadinessDeadline(100, 300, 100, 20, true) == 100);
  CHECK(ReadinessDeadline(UINT64_MAX - 1, 300, 100, 20, false) == UINT64_MAX);
  CHECK(ReadinessDeadline(100, 300, 100, 0, false) == 750000100);

  for (int fps : {30, 60, 120, 240}) {
    MotionTracker tracker;
    for (int frame = 0; frame <= fps * 2; ++frame) {
      const double seconds = static_cast<double>(frame) / fps;
      tracker.Update(1, static_cast<uint16_t>(frame),
                     1000000000ULL + static_cast<uint64_t>(std::llround(seconds * 1000000000.0)),
                     {40 * seconds, 0, 0});
    }
    CHECK(std::abs(tracker.ClosingSpeed({1000, 0, 0}) - 40) < .01);
    CHECK(tracker.ClosingSpeed({-1000, 0, 0}) == 0);
    CHECK(tracker.ClosingSpeed({NAN, 0, 0}) == 0);
    tracker.Update(1, static_cast<uint16_t>(fps * 2 + 1), 3010000000ULL, {10000, 0, 0});
    CHECK(tracker.speed() == 0);
    tracker.Update(2, 1, 3020000000ULL, {10001, 0, 0});
    CHECK(tracker.speed() == 0);
  }
  auto table = std::make_unique<DeadlineTable>();
  CHECK(table->Rank(10) == UINT64_MAX);
  table->Request(10, 100);
  table->Request(10, 200);
  CHECK(table->Rank(10) == 100);
  table->Request(10, 50);
  CHECK(table->Rank(10) == 50);
  table->Release(10);
  CHECK(table->Rank(10) == UINT64_MAX);
  table->Request(kInvalidEntry, 1);
  CHECK(table->Rank(kInvalidEntry) == UINT64_MAX);
  std::vector<std::thread> publishers;
  for (unsigned i = 1; i <= 16; ++i)
    publishers.emplace_back([&, i] { for (unsigned j = 0; j < 1000; ++j) table->Request(7, i); });
  for (auto& thread : publishers) thread.join();
  CHECK(table->Rank(7) == 1);
  table->Reset();
  CHECK(table->Rank(7) == UINT64_MAX);
}

void TraceCacheTests() {
  auto cache = std::make_unique<ClassificationTraceCache>();
  ClassificationTraceCache::Observation value{1, 1, 0x1000, 0x2000, 5, 7, 1, 255};
  CHECK(cache->ShouldEmit(value));
  CHECK(!cache->ShouldEmit(value));
  value.time = 2;
  CHECK(!cache->ShouldEmit(value));
  value.alpha = 127;
  CHECK(cache->ShouldEmit(value));
  CHECK(!cache->ShouldEmit(value));
  value.generation = 2;
  CHECK(cache->ShouldEmit(value));
  value.result = 3;
  CHECK(cache->ShouldEmit(value));
  value.entry = 8;
  CHECK(cache->ShouldEmit(value));
  value.view = 0x3000;
  CHECK(cache->ShouldEmit(value));
  cache->Reset();
  CHECK(cache->ShouldEmit(value));
  cache->Reset();
  // Exceed the fixed cache capacity and exercise replacement without growing
  // storage or suppressing an observation immediately after its insertion.
  for (uint32_t key = 1; key <= 100000; ++key) {
    value.entity = key * 16;
    value.time = key;
    CHECK(cache->ShouldEmit(value));
    CHECK(!cache->ShouldEmit(value));
  }
}

void AdmissionTests() {
  OrderedIoQueue q(4, 2);
  std::atomic<unsigned> published{0}, executed{0};
  auto item = std::make_shared<int>(7);
  std::weak_ptr<int> weak = item;
  CHECK(q.Admit(16, [owned = item, &executed] { ++executed; }, [&] { ++published; }) == Admission::kAccepted);
  CHECK(q.Admit(16, [&] { ++executed; }, [&] { ++published; }) == Admission::kAccepted);
  CHECK(q.Admit(16, [] {}, [&] { ++published; }) == Admission::kFull);
  CHECK(published == 2);
  item.reset();
  CHECK(!weak.expired());
  {
    Workers workers(q);
    q.Drain();
    CHECK(executed == 2);
    CHECK(weak.expired());
    CHECK(q.statistics().accepted == q.statistics().completed);
    q.Close();
    CHECK(q.Admit(32, [] {}, [&] { ++published; }) == Admission::kClosed);
    CHECK(published == 2);
  }
  CHECK(q.statistics().active == 0);
  CHECK(q.statistics().queued == 0);
  CHECK(q.statistics().high_water == 2);
}

void OrderingAndParallelismTests() {
  OrderedIoQueue q(4, 4096);
  Workers workers(q);
  std::atomic<bool> valid{true};
  std::array<unsigned, 32> next{};
  for (unsigned sequence = 0; sequence < 100; ++sequence) {
    for (unsigned file = 0; file < next.size(); ++file) {
      auto published = std::make_shared<std::atomic<bool>>(false);
      CHECK(q.Admit(static_cast<uintptr_t>(file + 1) << 4,
          [&, file, sequence, published] {
            if (!published->load() || next[file] != sequence) valid.store(false);
            ++next[file];
          }, [published] { published->store(true); }) == Admission::kAccepted);
    }
  }
  q.Drain();
  CHECK(valid.load());
  for (auto count : next) CHECK(count == 100);

  // Prove two independent lanes execute together, rather than merely testing
  // hash selection. The timeout makes a serialized implementation fail safely.
  std::mutex mutex;
  std::condition_variable ready;
  unsigned arrived = 0;
  std::atomic<bool> simultaneous{true};
  uintptr_t first = 16, second = 32;
  for (; q.LaneFor(first) == q.LaneFor(second); second += 16) {}
  auto overlap = [&] {
    std::unique_lock lock(mutex);
    ++arrived;
    ready.notify_all();
    if (!ready.wait_for(lock, std::chrono::seconds(3), [&] { return arrived == 2; })) simultaneous = false;
  };
  CHECK(q.Admit(first, overlap) == Admission::kAccepted);
  CHECK(q.Admit(second, overlap) == Admission::kAccepted);
  q.Drain();
  CHECK(simultaneous.load());
  CHECK(q.statistics().accepted == q.statistics().completed);
}

void ShutdownStressTests() {
  for (unsigned cycle = 0; cycle < 100; ++cycle) {
    OrderedIoQueue q(4, 128);
    Workers workers(q);
    auto lifetime = std::make_shared<int>(1);
    std::weak_ptr<int> weak = lifetime;
    std::atomic<unsigned> completed{0};
    unsigned accepted = 0;
    for (unsigned j = 0; j < 100; ++j) {
      const auto result = q.Admit(static_cast<uintptr_t>(j + 1) << 4,
                                  [&, hold = lifetime] { ++completed; });
      if (result == Admission::kAccepted) ++accepted;
    }
    lifetime.reset();
    q.Close();
    q.Drain();
    CHECK(completed == accepted);
    CHECK(weak.expired());
    const auto stats = q.statistics();
    CHECK(stats.accepted == stats.completed);
    CHECK(stats.queued == 0 && stats.active == 0);
  }
  // A producer racing Close may be accepted or rejected; accepted work must
  // always execute exactly once and rejected work must not publish PENDING.
  OrderedIoQueue q(4, 256);
  Workers workers(q);
  std::atomic<unsigned> accepted{0}, published{0}, executed{0};
  std::vector<std::thread> producers;
  for (unsigned i = 0; i < 8; ++i) producers.emplace_back([&, i] {
    for (unsigned j = 0; j < 1000; ++j) {
      if (q.Admit((i + 1) * 16, [&] { ++executed; }, [&] { ++published; }) == Admission::kAccepted)
        ++accepted;
    }
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  q.Close();
  for (auto& thread : producers) thread.join();
  q.Drain();
  CHECK(accepted == executed);
  CHECK(accepted == published);
}
// A pending transfer borrows guest memory. Merely retaining the thread object
// does not retain that memory after guest Exit frees its stack. Production's
// source-contract check requires this same admission-close/drain/exit order.
void DrainBeforeGuestReleaseTests() {
  OrderedIoQueue queue(1);
  Workers workers(queue);
  auto guest_buffer = std::make_unique<int>(47);
  int* borrowed = guest_buffer.get();
  std::latch transfer_started{1}, permit_transfer{1}, drain_started{1};
  std::atomic<bool> transfer_valid{false}, guest_exited{false};
  CHECK(queue.Admit(16, [&] {
    transfer_started.count_down();
    permit_transfer.wait();
    transfer_valid = *borrowed == 47;
  }) == Admission::kAccepted);
  transfer_started.wait();
  std::thread shutdown([&] {
    queue.Close();
    drain_started.count_down();
    queue.Drain();
    guest_buffer.reset();
    guest_exited = true;
  });
  drain_started.wait();
  const bool premature_exit = guest_exited.load();
  const auto refused = queue.Admit(16, [] {});
  permit_transfer.count_down();
  shutdown.join();
  CHECK(!premature_exit);
  CHECK(refused == Admission::kClosed);
  CHECK(transfer_valid.load());
  CHECK(guest_exited.load());
  CHECK(!guest_buffer);
  CHECK(queue.statistics().active == 0);
  CHECK(queue.statistics().accepted == queue.statistics().completed);
}

}  // namespace

int main() {
  try {
    PolicyTests();
    TraceCacheTests();
    AdmissionTests();
    OrderingAndParallelismTests();
    ShutdownStressTests();
    DrainBeforeGuestReleaseTests();
    std::cout << "PASS streaming modernization: " << checks << " checks\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
