#include <catch2/catch_test_macros.hpp>
#include "graphics/gta4_native/native_triangle_fan.h"
#include "graphics/gta4_native/native_owned_commands.h"
#include "graphics/gta4_native/native_pipeline_lookup_memo.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <algorithm>
using namespace rex::graphics::gta4_native;
namespace {
std::vector<uint8_t> Bytes(const std::vector<uint32_t>& values, bool wide) {
  std::vector<uint8_t> out(values.size()*(wide?4:2));
  for(size_t i=0;i<values.size();++i) {if(wide)std::memcpy(out.data()+i*4,&values[i],4);
    else {uint16_t v=uint16_t(values[i]);std::memcpy(out.data()+i*2,&v,2);}}
  return out;
}
std::vector<uint32_t> Oracle(const std::vector<uint32_t>& values,bool enabled,uint32_t restart,uint32_t mask) {
  std::vector<uint32_t> out,segment;
  const auto flush=[&] {for(size_t i=2;i<segment.size();++i)out.insert(out.end(),{segment[i-1],segment[i],segment[0]});segment.clear();};
  for(uint32_t v:values){if(enabled && (v&mask)==restart)flush();else segment.push_back(v);}flush();return out;
}
struct Command {
  static inline std::atomic<unsigned> moves=0,live=0;
  std::array<uint8_t,2768> embedded{};
  unsigned sequence=0;
  std::shared_ptr<unsigned> owner;
  Command(){++live;}
  ~Command(){--live;}
  Command(Command&& other):sequence(other.sequence),owner(std::move(other.owner)){++moves;++live;}
  Command& operator=(Command&&)=delete;
};
}
TEST_CASE("native fan exact counts restart offsets and incomplete segments", "[throughput][fan]") {
  for(bool wide:{false,true})for(bool enabled:{false,true})for(unsigned length=0;length<=9;++length) {
    const uint32_t restart=wide?UINT32_MAX:UINT16_MAX;
    for(unsigned pattern=0;pattern<(1u<<length);++pattern) {
      std::vector<uint32_t> input;
      for(unsigned i=0;i<length;++i)input.push_back((pattern&(1u<<i))?restart:i+3);
      const auto expected=Bytes(Oracle(input,enabled,restart,UINT32_MAX),wide);
      auto padded=input;padded.insert(padded.begin(),{123,124,125});padded.push_back(126);
      const auto actual=BuildNativeTriangleFan(Bytes(padded,wide),{3,length,restart,wide,enabled},UINT32_MAX);
      REQUIRE(actual);REQUIRE(*actual==expected);
    }
  }
  auto masked=BuildNativeTriangleFan(Bytes({1,2,3,0xABFFFFFFu,4,5,6},true),{0,7,0x00FFFFFFu,true,true},0x00FFFFFFu);
  REQUIRE(masked);CHECK(*masked==Bytes({2,3,1,5,6,4},true));
  CHECK_FALSE(BuildNativeTriangleFan({}, {UINT32_MAX,4,0,true,false},UINT32_MAX));
  CHECK_FALSE(BuildNativeTriangleFan(Bytes({1,2},false), {0,3,0,false,false},UINT32_MAX));
}
TEST_CASE("native fan sequential winding and cache lifetime", "[throughput][fan]") {
  std::vector<uint32_t> out(15);REQUIRE(WriteNativeSequentialFan(out,7,10));
  CHECK(out==std::vector<uint32_t>{11,12,10,12,13,10,13,14,10,14,15,10,15,16,10});
  CHECK_FALSE(WriteNativeSequentialFan(out,7,UINT32_MAX));
  CHECK_FALSE(WriteNativeSequentialFan(std::span<uint32_t>{},3));
  CHECK(WriteNativeSequentialFan(std::span<uint32_t>{},1));
  NativeTriangleFanCache cache;auto bytes=Bytes({0,1,2,3},true);
  auto first=cache.Get(bytes,{0,4,0,true,false},UINT32_MAX);REQUIRE(first);
  CHECK(cache.Get(bytes,{0,4,0,true,false},UINT32_MAX)==first);
  for(uint32_t i=1;i<100;++i)REQUIRE(cache.Get(bytes,{0,4,i,true,false},UINT32_MAX));
  CHECK(cache.size()==64);CHECK(*first==Bytes({1,2,0,2,3,0},true));
}
TEST_CASE("stable pooled command transport preserves identity owners and FIFO", "[throughput][transport]") {
  Command::moves=0;Command::live=0;
  NativeCommandPool<Command> pool;
  NativeOwnedCommands<Command,true> queue;
  NativeOwnedCommands<Command> batch,frame;
  auto held=std::make_shared<unsigned>(42);std::vector<const Command*> identities;
  for(unsigned i=0;i<1000;++i){auto c=pool.Make();c->sequence=i;c->owner=held;identities.push_back(c.get());queue.push_back(std::move(c));}
  for(unsigned i=0;i<1000;++i)batch.push_back(queue.TakeFront());
  CHECK(queue.empty());
  for(unsigned i=0;i<1000;++i){auto c=batch.Take(i);CHECK(c.get()==identities[i]);frame.push_back(std::move(c));}
  batch.clear();unsigned seq=0;for(const auto& c:frame){CHECK(c.sequence==seq++);CHECK(c.owner==held);}
  CHECK(Command::moves==0);CHECK(held.use_count()==1001);frame.clear();CHECK(Command::live==0);CHECK(held.use_count()==1);
}
TEST_CASE("pooled producer consumer concurrent reuse and cancellation", "[throughput][transport]") {
  NativeCommandPool<Command> pool;NativeOwnedCommands<Command,true> queue;
  std::mutex mutex;std::condition_variable cv;bool done=false;std::atomic<bool> ordered=true;
  std::thread producer([&]{for(unsigned i=0;i<20000;++i){auto p=pool.Make();p->sequence=i;{std::lock_guard lock(mutex);queue.push_back(std::move(p));}cv.notify_one();}{std::lock_guard lock(mutex);done=true;}cv.notify_one();});
  unsigned next=0;
  for(;;){NativeCommandPool<Command>::Owner p;{std::unique_lock lock(mutex);cv.wait(lock,[&]{return done||!queue.empty();});if(queue.empty())break;p=queue.TakeFront();}if(p->sequence!=next++)ordered=false;}
  producer.join();CHECK(next==20000);CHECK(ordered);CHECK(Command::live==0);
  for(unsigned i=0;i<100;++i)queue.push_back(pool.Make());queue.clear();CHECK(Command::live==0);
}
TEST_CASE("pipeline memo retains alternation and never crosses owner or lifetime", "[throughput][pipeline-memo]") {
  struct Fixed {unsigned v=0;bool operator==(const Fixed&)const=default;};
  NativePipelineLookupMemo<Fixed,4,uint64_t> memo;decltype(memo)::Context ctx;ctx.lifetime=1;int owner=0,other=0;
  for(unsigned i=0;i<4;++i)memo.Store(&owner,{i},ctx,i+1);
  for(unsigned i=0;i<4;++i)CHECK(memo.Find(&owner,{i},ctx)==i+1);
  CHECK(memo.Find(&other,{0},ctx)==0);++ctx.lifetime;CHECK(memo.Find(&owner,{0},ctx)==0);
}

TEST_CASE("command slab pool retires a full queue FIFO without upstream searches", "[throughput][transport][regression]") {
  NativeCommandPool<Command> pool;
  NativeOwnedCommands<Command, true> queue;
  constexpr unsigned count = 65536;
  for (unsigned i = 0; i < count; ++i) {
    auto value = pool.Make(); value->sequence = i; queue.push_back(std::move(value));
  }
  const auto before = pool.GetStatistics();
  REQUIRE(before.live == count);
  CHECK(before.slabs == count / NativeCommandPool<Command>::kSlotsPerSlab);
  for (unsigned i = 0; i < count; ++i) {
    auto value = queue.TakeFront(); REQUIRE(value->sequence == i);
  }
  const auto retired = pool.GetStatistics();
  CHECK(retired.live == 0);
  CHECK(retired.free == count);
  CHECK(retired.slabs == before.slabs);
  for (unsigned i = 0; i < count; ++i) queue.push_back(pool.Make());
  CHECK(pool.GetStatistics().slabs == before.slabs);
  queue.clear();
  CHECK(pool.GetStatistics().live == 0);
}

TEST_CASE("command slabs preserve over-alignment and recover throwing construction", "[throughput][transport][regression]") {
  struct alignas(256) Aligned {
    unsigned value;
    explicit Aligned(bool fail) : value(123) { if (fail) throw 7; }
  };
  NativeCommandPool<Aligned> pool;
  CHECK_THROWS_AS(pool.Make(true), int);
  CHECK(pool.GetStatistics().live == 0);
  auto object = pool.Make(false);
  CHECK(reinterpret_cast<uintptr_t>(object.get()) % alignof(Aligned) == 0);
  CHECK(object->value == 123);
  CHECK(pool.GetStatistics().slabs == 1);
  object.reset();
  CHECK(pool.GetStatistics().live == 0);
  CHECK(pool.GetStatistics().free == NativeCommandPool<Aligned>::kSlotsPerSlab);
}

TEST_CASE("command slabs permit parallel producer and consumer ownership", "[throughput][transport][regression]") {
  NativeCommandPool<Command> pool;
  std::atomic<unsigned> finished{0};
  std::vector<std::thread> threads;
  for (unsigned thread = 0; thread < 4; ++thread) {
    threads.emplace_back([&] {
      std::vector<NativeCommandPool<Command>::Owner> owners;
      for (unsigned repeat = 0; repeat < 8; ++repeat) {
        for (unsigned i = 0; i < 512; ++i) owners.push_back(pool.Make());
        std::reverse(owners.begin(), owners.end());
        owners.clear();
      }
      ++finished;
    });
  }
  for (auto& thread : threads) thread.join();
  CHECK(finished == 4);
  CHECK(pool.GetStatistics().live == 0);
  CHECK(Command::live == 0);
}

TEST_CASE("completed command batches retire once with mixed pools and moved slots",
          "[throughput][transport][batch]") {
  NativeCommandPool<Command> first, second;
  NativeOwnedCommands<Command> batch;
  auto shared = std::make_shared<unsigned>(9);
  for (unsigned i = 0; i < 8192; ++i) {
    auto value = i % 3 ? first.Make() : second.Make();
    value->owner = shared;
    batch.push_back(std::move(value));
  }
  auto survivor = batch.Take(0);
  batch.clear(first);
  CHECK(batch.empty());
  CHECK(first.GetStatistics().live == 0);
  CHECK(second.GetStatistics().live == 1);
  CHECK(shared.use_count() == 2);
  survivor.reset();
  CHECK(second.GetStatistics().live == 0);
  CHECK(shared.use_count() == 1);
  batch.clear(first);
}

TEST_CASE("batched command retirement permits concurrent allocation",
          "[throughput][transport][batch]") {
  NativeCommandPool<Command> pool;
  std::vector<std::thread> workers;
  for (unsigned i = 0; i < 4; ++i) workers.emplace_back([&] {
    for (unsigned iteration = 0; iteration < 30; ++iteration) {
      NativeOwnedCommands<Command> batch;
      for (unsigned n = 0; n < 256; ++n) batch.push_back(pool.Make());
      batch.clear(pool);
    }
  });
  for (auto& worker : workers) worker.join();
  CHECK(pool.GetStatistics().live == 0);
  CHECK(Command::live == 0);
}
