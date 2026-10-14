// Real generated PPC and production lifts, with deterministic external module
// callbacks. Synthetic guest memory isolates selection/fade contracts from the
// rest of the game; this is not a substitute for a gameplay capture.
#include "gta4_init.h"
#include "gta4_streaming_policy.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <sys/mman.h>
#include <tuple>
#include <vector>

namespace {
size_t checks = 0;
void Check(bool value, const char* text, int line) {
  ++checks;
  if (!value) throw std::runtime_error(std::to_string(line) + ": " + text);
}
#define CHECK(value) Check(bool(value), #value, __LINE__)
constexpr uint32_t kManager = 0x82A9AA7C;
constexpr uint32_t kTable = 0x200000;
constexpr uint32_t kEntity = 0x300000;
constexpr uint32_t kParent = 0x301000;
constexpr uint32_t kModel = 0x302000;
constexpr uint32_t kView = 0x303000;
constexpr uint32_t kVtable = 0x304000;
constexpr size_t kEntries = 64;
struct { bool modern = true; } config;
struct { uint32_t entity = kEntity; } world;
bool deadline_order = false;
float margin = 50;
bool occlusion = false;
std::unique_ptr<gta4::streaming::DeadlineTable> deadline_table;
std::array<std::vector<uint32_t>, kEntries> dependencies;
using Callback = std::tuple<unsigned, uint32_t, uint32_t>;
std::vector<Callback> callbacks;

bool DeadlineOrderEnabled(uint8_t*, uint32_t manager) { return deadline_order && manager == kManager; }
uint64_t QueueRank(uint32_t index) { return deadline_table->Rank(index); }
bool RankBefore(uint64_t rank, uint32_t sector, uint64_t best_rank, uint32_t best_sector) {
  return rank < best_rank || (rank == best_rank && sector < best_sector);
}
float EntityPreloadMargin(uint8_t*, uint32_t, uint32_t, double, double, float) { return margin; }
void ObserveClassification(uint8_t*, uint32_t, uint32_t, double, uint32_t) {}
void RangeCallback(PPCContext& ctx, uint8_t*) { ctx.f1.f64 = 1000; }
void PrepareCallback(PPCContext& ctx, uint8_t*) { callbacks.emplace_back(5, ctx.r3.u32, 0); }
}

namespace rex::runtime {
PPCFunc* ResolveIndirectFunction(uint32_t address) {
  if (address == 1) return RangeCallback;
  if (address == 2) return PrepareCallback;
  throw std::runtime_error("Unexpected indirect target " + std::to_string(address));
}
}

extern "C" void sub_8250D328(PPCContext& ctx, uint8_t* base) {
  const auto& deps = dependencies.at(ctx.r4.u32);
  for (size_t index = 0; index < deps.size(); ++index)
    REX_STORE_U32(ctx.r5.u32 + static_cast<uint32_t>(index * 4), deps[index]);
  ctx.r3.u64 = deps.size();
}
extern "C" void sub_825120E8(PPCContext& ctx, uint8_t*) {
  callbacks.emplace_back(1, ctx.r4.u32, ctx.r5.u32);
  ctx.r3.u64 = 1;
}
extern "C" void sub_82512BF0(PPCContext& ctx, uint8_t* base) {
  callbacks.emplace_back(2, ctx.r4.u32, 0);
  const uint32_t entry = kTable + ctx.r4.u32 * 24;
  REX_STORE_U32(entry + 8, (REX_LOAD_U32(entry + 8) & 0x3FFFFFFF) | 0x40000000);
}
extern "C" void sub_821D6DD0(PPCContext& ctx, uint8_t*) { ctx.r3.u64 = occlusion ? 1 : 0; }
extern "C" void sub_822A2AA8(PPCContext& ctx, uint8_t*) { ctx.r3.u64 = 0; }
extern "C" void sub_821D75B8(PPCContext& ctx, uint8_t*) {
  callbacks.emplace_back(3, ctx.r3.u32, std::bit_cast<uint32_t>(float(ctx.f1.f64)));
}
extern "C" void sub_821D7900(PPCContext& ctx, uint8_t*) {
  callbacks.emplace_back(4, ctx.r3.u32, ctx.r5.u32);
}

// Emitted from reviewed source identities by run_streaming_guest_tests.py.
// The original save/restore helpers are included, not reimplemented.
#include "streaming_retail_reference.inc"
#include "gta4_streaming_guest.inc"

namespace {
struct GuestMemory {
  uint8_t* base = nullptr;
  GuestMemory() {
    void* mapped = mmap(nullptr, REX_MEMORY_SIZE, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, -1, 0);
    if (mapped == MAP_FAILED) throw std::runtime_error("Cannot reserve synthetic guest memory");
    base = static_cast<uint8_t*>(mapped);
  }
  ~GuestMemory() { if (base) munmap(base, REX_MEMORY_SIZE); }
};
void Float(uint8_t* base, uint32_t address, float value) {
  REX_STORE_U32(address, std::bit_cast<uint32_t>(value));
}
PPCContext Context() {
  PPCContext ctx{};
  ctx.r1.u64 = 0x180000;
  ctx.lr = 0x12340000;
  ctx.r14.u64 = 14; ctx.r15.u64 = 15; ctx.r16.u64 = 16; ctx.r17.u64 = 17;
  ctx.r18.u64 = 18; ctx.r19.u64 = 19; ctx.r20.u64 = 20; ctx.r21.u64 = 21;
  ctx.r22.u64 = 22; ctx.r23.u64 = 23; ctx.r24.u64 = 24; ctx.r25.u64 = 25;
  ctx.r26.u64 = 26; ctx.r27.u64 = 27; ctx.r28.u64 = 28; ctx.r29.u64 = 29;
  ctx.r30.u64 = 30; ctx.r31.u64 = 31;
  ctx.f27.f64 = 27; ctx.f28.f64 = 28; ctx.f29.f64 = 29;
  ctx.f30.f64 = 30; ctx.f31.f64 = 31;
  return ctx;
}
void CheckPreserved(const PPCContext& ctx) {
  CHECK(ctx.r1.u64 == 0x180000);
  CHECK(ctx.lr == 0x12340000);
  CHECK(ctx.r14.u64 == 14 && ctx.r15.u64 == 15 && ctx.r16.u64 == 16 && ctx.r17.u64 == 17);
  CHECK(ctx.r18.u64 == 18 && ctx.r19.u64 == 19 && ctx.r20.u64 == 20 && ctx.r21.u64 == 21);
  CHECK(ctx.r22.u64 == 22 && ctx.r23.u64 == 23 && ctx.r24.u64 == 24 && ctx.r25.u64 == 25);
  CHECK(ctx.r26.u64 == 26 && ctx.r27.u64 == 27 && ctx.r28.u64 == 28 && ctx.r29.u64 == 29);
  CHECK(ctx.r30.u64 == 30 && ctx.r31.u64 == 31);
  CHECK(ctx.f27.f64 == 27 && ctx.f28.f64 == 28 && ctx.f29.f64 == 29 && ctx.f30.f64 == 30 && ctx.f31.f64 == 31);
}
struct QueueEntry { uint32_t sector = 0, bytes = 8192; uint16_t flags = 0; };
void SetupQueue(uint8_t* base, const std::vector<QueueEntry>& entries) {
  std::memset(base + kManager, 0, 256);
  std::memset(base + kTable, 0, kEntries * 24);
  REX_STORE_U32(0x83032744, kTable);
  REX_STORE_U32(kManager, kTable);
  REX_STORE_U32(kManager + 16, kTable);
  REX_STORE_U32(kManager + 20, kTable + 24);
  REX_STORE_U16(kTable + 16, entries.empty() ? 1 : 2);
  REX_STORE_U16(kTable + 18, 0xFFFF);
  REX_STORE_U16(kTable + 24 + 16, 0xFFFF);
  REX_STORE_U16(kTable + 24 + 18, entries.empty() ? 0 : static_cast<uint16_t>(entries.size() + 1));
  unsigned priorities = 0, nondummy = 0;
  for (size_t index = 0; index < entries.size(); ++index) {
    const QueueEntry& source = entries[index];
    const uint32_t entry = kTable + static_cast<uint32_t>(index + 2) * 24;
    REX_STORE_U32(entry + 4, source.sector);
    REX_STORE_U32(entry + 8, 0x80000000u | source.bytes);
    REX_STORE_U16(entry + 14, source.flags);
    REX_STORE_U16(entry + 16, index + 1 == entries.size() ? 1 : static_cast<uint16_t>(index + 3));
    REX_STORE_U16(entry + 18, index == 0 ? 0 : static_cast<uint16_t>(index + 1));
    priorities += (source.flags & 0x10) != 0;
    nondummy += (source.flags & 0x800) == 0;
  }
  REX_STORE_U32(kManager + 56, static_cast<uint32_t>(entries.size()));
  REX_STORE_U32(kManager + 60, nondummy);
  REX_STORE_U32(kManager + 64, priorities);
  callbacks.clear();
}
struct QueueResult {
  uint32_t selection;
  std::array<uint8_t, 256> manager;
  std::array<uint8_t, kEntries * 24> table;
  std::vector<Callback> effects;
};
QueueResult RunQueue(uint8_t* base, const std::vector<QueueEntry>& entries, bool modern,
                     uint32_t cursor = 0, bool priority_filter = true) {
  SetupQueue(base, entries);
  // Dependency rows are outside the pending list.
  for (uint32_t index = 48; index < kEntries; ++index)
    REX_STORE_U32(kTable + index * 24 + 8, (index % 4) << 30);
  auto ctx = Context();
  ctx.r3.u64 = kManager; ctx.r4.u64 = cursor; ctx.r5.u64 = priority_filter;
  deadline_order = modern;
  sub_825132D0(ctx, base);
  CheckPreserved(ctx);
  QueueResult result{};
  result.selection = ctx.r3.u32;
  std::memcpy(result.manager.data(), base + kManager, result.manager.size());
  std::memcpy(result.table.data(), base + kTable, result.table.size());
  result.effects = callbacks;
  return result;
}
void QueueTests(uint8_t* base) {
  std::mt19937 random(0x47A4);
  for (unsigned iteration = 0; iteration < 1500; ++iteration) {
    for (auto& list : dependencies) list.clear();
    deadline_table->Reset();
    const size_t count = random() % 30;
    std::vector<QueueEntry> entries;
    for (size_t index = 0; index < count; ++index) {
      QueueEntry entry{random() % 10000, 8192, 0};
      if (random() % 3 == 0) entry.flags |= 0x10;
      if (random() % 4 == 0) { entry.flags |= 0x800; entry.bytes = 0; }
      if (index + 1 != count && random() % 3 == 0)
        dependencies[index + 2].push_back(48 + random() % 16);
      if (index + 1 == count) { entry.flags = 0x10; entry.bytes = 8192; }
      entries.push_back(entry);
    }
    const auto retail = RunQueue(base, entries, false);
    const auto modern = RunQueue(base, entries, true);
    CHECK(retail.selection == modern.selection);
    CHECK(retail.manager == modern.manager);
    CHECK(retail.table == modern.table);
    CHECK(retail.effects == modern.effects);
  }
  for (auto& list : dependencies) list.clear();
  deadline_table->Reset();
  std::vector<QueueEntry> entries{{100,8192,0},{900,8192,0},{500,8192,0}};
  CHECK(RunQueue(base,entries,false).selection == 2);
  deadline_table->Request(3, 1);
  CHECK(RunQueue(base,entries,true).selection == 3);
  // Earliest deadline wins even behind the simulated disk head.
  deadline_table->Request(2, 1);
  CHECK(RunQueue(base,entries,true,600).selection == 2);
  CHECK(RunQueue(base,entries,false,600).selection == 3);
  // An urgent rank may not bypass an unavailable dependency.
  dependencies[2] = {48};
  CHECK(RunQueue(base,entries,true).selection == 3);
  CHECK(!callbacks.empty());
  // Existing explicit priorities continue to take precedence.
  dependencies[2].clear();
  entries[2].flags = 0x10;
  CHECK(RunQueue(base,entries,true).selection == 4);
}

struct EntityResult {
  uint32_t result;
  std::array<uint8_t, 512> entity, parent;
  std::vector<Callback> effects;
};
EntityResult RunEntity(uint8_t* base, bool modern, float test_margin, bool loaded, bool parent,
                       bool main_view, uint8_t alpha, float distance, bool occluded, bool allowed = true) {
  std::memset(base + kEntity, 0, 0x6000);
  REX_STORE_U32(kEntity, kVtable);
  REX_STORE_U32(kVtable + 84, 1);
  REX_STORE_U32(kVtable + 60, 2);
  REX_STORE_U32(kEntity + 36, 0x04000000);
  REX_STORE_U32(kEntity + 76, parent ? kParent : 0);
  Float(base, kEntity + 80, 80);
  REX_STORE_U8(kEntity + 99, alpha);
  REX_STORE_U8(kParent + 99, 173);
  REX_STORE_U8(kParent + 97, 2);
  REX_STORE_U32(kModel + 64, loaded ? 8 : 0);
  REX_STORE_U8(kView + 26, main_view);
  Float(base, kView + 2340, 1);
  Float(base, 0x82000A2C, 50);
  Float(base, 0x82000A34, 0);
  Float(base, 0x82000D48, 1);
  Float(base, 0x82000D74, .5);
  Float(base, 0x82000DC8, 2);
  Float(base, 0x82000E0C, 20);
  Float(base, 0x82002518, 80);
  Float(base, 0x8200251C, 10);
  Float(base, 0x820BEDD4, 255);
  Float(base, 0x82B6F5C8, 1000);
  auto ctx = Context();
  ctx.r3.u64 = kEntity; ctx.r4.u64 = kModel;
  ctx.r6.u64 = allowed; ctx.r7.u64 = 0; ctx.r8.u64 = kView; ctx.f1.f64 = distance;
  config.modern = modern; margin = test_margin; occlusion = occluded; callbacks.clear();
  sub_821D8848(ctx, base);
  CheckPreserved(ctx);
  EntityResult result{};
  result.result = ctx.r3.u32;
  std::memcpy(result.entity.data(), base + kEntity, result.entity.size());
  std::memcpy(result.parent.data(), base + kParent, result.parent.size());
  result.effects = callbacks;
  return result;
}
void EntityTests(uint8_t* base) {
  unsigned additional_preloads = 0;
  for (bool loaded : {false,true}) for (bool parent : {false,true})
  for (bool main_view : {false,true}) for (uint8_t alpha : {0,64,255})
  for (bool occluded : {false,true}) for (unsigned distance = 0; distance <= 300; distance += 5) {
    const auto retail = RunEntity(base,false,50,loaded,parent,main_view,alpha,float(distance),occluded);
    const auto identity = RunEntity(base,true,50,loaded,parent,main_view,alpha,float(distance),occluded);
    CHECK(retail.result == identity.result);
    CHECK(retail.entity == identity.entity);
    CHECK(retail.parent == identity.parent);
    CHECK(retail.effects == identity.effects);
    const auto earlier = RunEntity(base,true,100,loaded,parent,main_view,alpha,float(distance),occluded);
    // No entity or LOD-parent fade bytes/list flags may be modified by widening
    // this operand. Differences must only admit earlier request classification.
    CHECK(retail.entity == earlier.entity);
    CHECK(retail.parent == earlier.parent);
    if (retail.result != earlier.result) {
      CHECK(retail.result == 0 && earlier.result == 3);
      ++additional_preloads;
    }
  }
  CHECK(additional_preloads > 0);
  const auto disallowed = RunEntity(base,true,250,false,false,true,255,200,false,false);
  CHECK(disallowed.result == 0);
  std::cout << "Additional preload-only admissions tested: " << additional_preloads << '\n';
}
}

int main() {
  try {
    GuestMemory memory;
    deadline_table = std::make_unique<gta4::streaming::DeadlineTable>();
    QueueTests(memory.base);
    EntityTests(memory.base);
    deadline_table.reset();
    std::cout << "PASS compiled streaming guest equivalence: " << checks << " checks\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL compiled streaming guest equivalence: " << error.what() << '\n';
    return 1;
  }
}
