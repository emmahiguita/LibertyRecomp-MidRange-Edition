// Exercise production request/state/trace hooks. External game allocation and
// dependency callbacks are fixture boundaries, not substitutes for gameplay.
#include "gta4_init.h"
#undef REXLOG_INFO
#undef REXLOG_WARN
#undef REXLOG_ERROR
#define REXLOG_INFO(...) ((void)0)
#define REXLOG_WARN(...) ((void)0)
#define REXLOG_ERROR(...) ((void)0)
#include "../../glue/rexglue-sdk-main/gta4-recomp/src/gta4_streaming_hooks.cpp"
#include <sys/mman.h>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace gta4::streaming;
namespace {
size_t checks = 0;
void Check(bool value, int line) {
  ++checks;
  if (!value) throw std::runtime_error("Lifecycle check failed at " + std::to_string(line));
}
#define CHECK(value) Check(bool(value), __LINE__)
constexpr uint32_t kTestTable = 0x100000;
constexpr uint32_t kOtherTable = 0x200000;
constexpr uint32_t kTestEntity = 0x300000;
constexpr uint32_t kTestView = 0x301000;
std::atomic<uint32_t> last_request_flags{0};
bool refuse_request = false;
uint32_t allocator_capacity = 4096;
struct Arena {
  uint8_t* base;
  Arena() : base(static_cast<uint8_t*>(mmap(nullptr, REX_MEMORY_SIZE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANON, -1, 0))) {
    if (base == MAP_FAILED) throw std::runtime_error("Guest reservation failed");
  }
  ~Arena() { munmap(base, REX_MEMORY_SIZE); }
};
void SetTable(uint8_t* base, uint32_t table) {
  REX_STORE_U32(kEntriesGlobal, table);
  REX_STORE_U32(kManager, table);
}
void State(uint8_t* base, uint32_t table, uint32_t id, uint32_t state) {
  PPCContext ctx{};
  ctx.r3.u32 = table + id * kEntryStride;
  ctx.r4.u32 = state;
  sub_82511890(ctx, base);
}
void Request(uint8_t* base, uint32_t id, uint32_t flags) {
  PPCContext ctx{};
  ctx.r3.u32 = kManager;
  ctx.r4.u32 = id;
  ctx.r5.u32 = flags;
  sub_825120E8(ctx, base);
}
}

// The real state hook remains under test; only the external guest operation
// that writes the packed state is isolated from its separate dependency graph.
extern "C" void __imp__sub_82511890(PPCContext& ctx, uint8_t* base) {
  const uint32_t packed = REX_LOAD_U32(ctx.r3.u32 + 8);
  REX_STORE_U32(ctx.r3.u32 + 8, (packed & kSizeMask) | ((ctx.r4.u32 & 3) << kStateShift));
}
extern "C" void __imp__sub_825120E8(PPCContext& ctx, uint8_t* base) {
  last_request_flags = ctx.r5.u32;
  if (refuse_request) { ctx.r3.u64 = 0; return; }
  const uint32_t table = REX_LOAD_U32(ctx.r3.u32);
  State(base, table, ctx.r4.u32, 2);
  ctx.r3.u64 = 1;
}
extern "C" void __imp__sub_8251D960(PPCContext&, uint8_t*) {}
extern "C" void __imp__sub_82511C50(PPCContext& ctx, uint8_t* base) {
  REX_STORE_U32(ctx.r3.u32 + 32, std::min(ctx.r4.u32, allocator_capacity));
}
extern "C" void __imp__sub_82511CD8(PPCContext& ctx, uint8_t* base) {
  REX_STORE_U32(ctx.r3.u32 + 44, std::min(ctx.r4.u32, allocator_capacity));
}
extern "C" void sub_8250D328(PPCContext& ctx, uint8_t*) { ctx.r3.u64 = 0; }
extern "C" void sub_82512BF0(PPCContext&, uint8_t*) {}
extern "C" void sub_821D6DD0(PPCContext& ctx, uint8_t*) { ctx.r3.u64 = 0; }
extern "C" void sub_822A2AA8(PPCContext& ctx, uint8_t*) { ctx.r3.u64 = 0; }
extern "C" void sub_821D75B8(PPCContext&, uint8_t*) {}
extern "C" void sub_821D7900(PPCContext&, uint8_t*) {}
// Exact generated fallback bodies and ABI register helpers.
#include "streaming_original_fixture.inc"

int main() {
  try {
    Arena arena;
    uint8_t* base = arena.base;
    Initialize(base);
    config.modern = true;
    config.deadline_order = true;
    SetTable(base, kTestTable);
    world.entity = kTestEntity;
    world.view = kTestView;
    world.model = 123;
    world.now = Now();
    world.deadline = world.now;
    Request(base, 7, 0);
    CHECK((last_request_flags & 0x10) != 0);
    CHECK(records[7].generation == 1);
    CHECK(records[7].entity == kTestEntity);
    CHECK(deadlines.Rank(7) >= world.now);
    CHECK(deadlines.Rank(7) <= Now());
    CHECK(records[7].request_time >= world.now);
    CHECK(records[7].request_time <= Now());
    State(base, kTestTable, 7, 1);
    CHECK(records[7].ready_time != 0);
    const auto first_ready = records[7].ready_time;
    const auto first_latency = observed_world_latency.load();
    State(base, kTestTable, 7, 3);
    State(base, kTestTable, 7, 1);
    CHECK(records[7].ready_time == first_ready);
    CHECK(observed_world_latency.load() == first_latency);
    State(base, kTestTable, 7, 0);
    CHECK(records[7].request_time == 0 && records[7].ready_time == 0);
    CHECK(records[7].entity == 0);
    CHECK(deadlines.Rank(7) == UINT64_MAX);
    Request(base, 7, 0);
    CHECK(records[7].generation == 2);
    CHECK(reloads == 1);

    // Every old compact index loses its rank when the backing table changes.
    deadlines.Request(41, 1);
    const uint64_t old_epoch = epoch.load();
    SetTable(base, kOtherTable);
    CHECK(DeadlineOrderEnabled(base, kManager));
    CHECK(epoch.load() > old_epoch);
    CHECK(records[7].generation == 0);
    CHECK(deadlines.Rank(7) == UINT64_MAX);
    CHECK(deadlines.Rank(41) == UINT64_MAX);
    Request(base, 7, 0);
    CHECK(records[7].generation == 1);
    CHECK(records[7].table == kOtherTable);
    State(base, kOtherTable, 7, 0);

    // Refusal removes provisional host bookkeeping, never a guest reference.
    refuse_request = true;
    Request(base, 9, 0);
    CHECK(deadlines.Rank(9) == UINT64_MAX);
    CHECK(records[9].request_time == 0);
    CHECK(records[9].entity == 0);
    refuse_request = false;

    // Unscoped requests do not acquire the new world-priority flag.
    world = {};
    Request(base, 10, 0);
    CHECK(last_request_flags == 0);
    State(base, kOtherTable, 10, 0);
    world = {kTestEntity, kTestView, 123, Now(), 0, 0, 0};
    ObserveClassification(base, kTestEntity, kTestView, 100, 2);
    CHECK(world.deadline == world.now);

    // Budget argument scaling still cannot defeat the allocator's clamp.
    config.budget_scale = 2;
    config.physical_reserve_bytes = 512;
    PPCContext budget{};
    budget.r3.u32 = kManager; budget.r4.u32 = 3000;
    sub_82511CD8(budget, base);
    CHECK(REX_LOAD_U32(kManager + 44) == 3584);
    budget.r3.u32 = kManager; budget.r4.u32 = 0;
    sub_82511C50(budget, base);
    CHECK(REX_LOAD_U32(kManager + 32) == 0);

    // Fixed trace storage survives an episode reset without reopening/truncation.
    trace.file = std::tmpfile();
    CHECK(trace.file != nullptr);
    trace.attempted = true;
    trace_enabled = true;
    EventLocked({Now(), 0, 1, 7, kTestEntity, 123, 1, 0, 2, 0, 0});
    const uint64_t prior_event_epoch = events[0].epoch;
    auto* file = trace.file;
    Initialize(base);
    CHECK(trace.file == file);
    CHECK(event_count == 1);
    CHECK(events[0].epoch == prior_event_epoch);
    CHECK(epoch.load() > prior_event_epoch);
    CHECK(trace_enabled.load());
    // A full event buffer must not consume a classification-cache update:
    // the unchanged observation must be retried after a batch drains.
    BindTable(kOtherTable);
    world = {kTestEntity, kTestView, 123, Now(), 0, 0, 0};
    const size_t saved_count = event_count;
    event_count = kTraceCapacity;
    const auto saved_dropped = dropped_events;
    ObserveClassification(base, kTestEntity, kTestView, 100, 2);
    CHECK(dropped_events == saved_dropped + 1);
    event_count = saved_count;
    ObserveClassification(base, kTestEntity, kTestView, 100, 2);
    CHECK(event_count == saved_count + 1);
    ObserveClassification(base, kTestEntity, kTestView, 100, 2);
    CHECK(event_count == saved_count + 1);
    event_count = saved_count;
    dropped_events = saved_dropped;
    for (size_t i = 1; i < kTraceCapacity; ++i)
      EventLocked({Now(), 0, 1, 7, kTestEntity, 123, 2, 2, 1, 0, 0});
    EventLocked({});
    CHECK(event_count == kTraceCapacity);
    CHECK(dropped_events == 1);
    const auto before_written = written_events;
    FinishTrace();
    CHECK(trace.file == nullptr);
    CHECK(!classification_cache);
    CHECK(!trace_enabled.load());
    CHECK(event_count == 0);
    CHECK(written_events == before_written + kTraceCapacity);
    FinishTrace();
    CHECK(trace.file == nullptr);
    // Distinct real request/state hooks run concurrently against one table.
    // Guest callbacks operate on separate entry rows, as required by their
    // caller-side ownership; shared production side data must still be safe.
    config.modern = true;
    config.deadline_order = true;
    SetTable(base, kOtherTable);
    BindTable(kOtherTable);
    constexpr unsigned thread_count = 4;
    constexpr unsigned cycles = 100;
    std::vector<std::thread> threads;
    for (unsigned thread = 0; thread < thread_count; ++thread) {
      threads.emplace_back([&, thread] {
        const uint32_t id = 128 + thread;
        for (unsigned cycle = 0; cycle < cycles; ++cycle) {
          world = {kTestEntity, kTestView, 123, Now(), 0, 0, 0};
          world.deadline = world.now;
          Request(base, id, 0);
          State(base, kOtherTable, id, 1);
          State(base, kOtherTable, id, 0);
        }
      });
    }
    for (auto& thread : threads) thread.join();
    for (unsigned thread = 0; thread < thread_count; ++thread) {
      const uint32_t id = 128 + thread;
      CHECK(records[id].generation == cycles);
      CHECK(records[id].request_time == 0 && records[id].ready_time == 0);
      CHECK(deadlines.Rank(id) == UINT64_MAX);
    }
    std::cout << "PASS production streaming lifecycle: " << checks << " checks\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
