#include <catch2/catch_test_macros.hpp>
#include <array>
#include <string>
#include <thread>
#include <vector>
#include "graphics/gta4_native/native_gpu_scope_summary_labels.h"
using namespace rex::graphics::gta4_native;
namespace {
NativeGpuScopeKey Key(uint64_t vs=1, uint64_t ps=2, uint32_t phase=31) {
  return {.vertex_shader=vs,.pixel_shader=ps,.retail_phase=phase,
          .semantic_phase=1,.source=GpuPassOriginSource::kListHeader};
}
}
TEST_CASE("Scope metadata is inactive outside an existing render scope", "[graphics][gpu-labels]") {
  NativeGpuScopeSummary s;
  s.Observe(Key(),"v","p",3,9,3,0); s.ObserveClear();
  REQUIRE(s.entries().empty()); REQUIRE(s.draws()==0); REQUIRE(s.clears()==0);
  s.Begin(100,7); s.Observe(Key(),"v","p",3,9,0,0);
  REQUIRE(s.entries().empty()); REQUIRE(s.frame()==100); REQUIRE(s.first_command()==7);
  s.Observe(Key(),"v","p",3,9,3,0); s.End();
  s.Observe(Key(9),"v","p",3,9,3,0); s.ObserveClear();
  REQUIRE(s.draws()==1); REQUIRE(s.entries().size()==1); REQUIRE(s.clears()==0);
  size_t labels=0; EmitNativeGpuScopeLabels(s,[&](const auto&){++labels;}); REQUIRE(labels==0);
}
TEST_CASE("Scope counts issued host draws and keeps clear work separate", "[graphics][gpu-labels]") {
  NativeGpuScopeSummary s;s.Begin(1,2);
  s.Observe(Key(),"v","p",5,10,3,0);s.Observe(Key(),"v","p",5,11,0,6);s.ObserveClear();
  REQUIRE(s.draws()==2);REQUIRE(s.vertices()==3);REQUIRE(s.indices()==6);REQUIRE(s.clears()==1);
  REQUIRE(s.entries().size()==1);const auto& e=s.entries()[0];
  REQUIRE(e.draws==2);REQUIRE(e.vertices==3);REQUIRE(e.indices==6);
  REQUIRE(e.first_list_scope==10);REQUIRE(e.last_list_scope==11);REQUIRE_FALSE(e.mixed_pipelines);
  s.Observe(Key(),"v","p",6,12,3,0);REQUIRE(s.entries()[0].mixed_pipelines);
  REQUIRE(s.entries()[0].first_pipeline==5);
}
TEST_CASE("Different stage shader phase and provenance identities stay separate", "[graphics][gpu-labels]") {
  NativeGpuScopeSummary s;s.Begin(1,0);const auto key=Key();
  std::array<NativeGpuScopeKey,6> keys;keys.fill(key);
  keys[1].vertex_shader=9;keys[2].pixel_shader=9;keys[3].retail_phase=1;
  keys[4].semantic_phase=4;keys[5].source=GpuPassOriginSource::kMismatch;
  for(const auto& k:keys)s.Observe(k,"v","p",1,1,3,0);
  REQUIRE(s.entries().size()==keys.size());
  for(size_t i=0;i<keys.size();++i)REQUIRE(s.entries()[i].key==keys[i]);
}
TEST_CASE("Overflow remains visible and does not allocate extra entries", "[graphics][gpu-labels]") {
  NativeGpuScopeSummary s;s.Begin(1,0);
  for(uint32_t i=0;i<128;++i)s.Observe(Key(i,2),"v","p",1,1,3,0);
  REQUIRE(s.entries().size()==64);REQUIRE(s.overflow_draws()==64);REQUIRE(s.draws()==128);
  s.Observe(Key(0,2),"v","p",2,2,0,6);
  REQUIRE(s.entries()[0].draws==2);REQUIRE(s.entries()[0].mixed_pipelines);
  REQUIRE(s.entries().size()==64);REQUIRE(s.overflow_draws()==64);REQUIRE(s.indices()==6);
  s.End();s.Begin(2,19);REQUIRE(s.serial()==2);REQUIRE(s.draws()==0);
  REQUIRE(s.entries().empty());REQUIRE(s.overflow_draws()==0);REQUIRE(s.indices()==0);
}
TEST_CASE("Geometry totals exceed 32 bit without wrapping", "[graphics][gpu-labels]") {
  NativeGpuScopeSummary s;s.Begin(1,0);
  s.Observe(Key(),"v","p",1,1,UINT32_MAX,0);s.Observe(Key(),"v","p",1,1,UINT32_MAX,0);
  REQUIRE(s.vertices()==UINT64_C(8589934590));REQUIRE(s.entries()[0].vertices==s.vertices());
}
TEST_CASE("Production summary serialization preserves all attribution fields", "[graphics][gpu-labels]") {
  NativeGpuScopeSummary s;s.Begin(200,19);s.Observe(Key(),"fixture_vs","fixture_ps",0xAB,9,0,6);
  std::vector<std::string> labels;EmitNativeGpuScopeLabels(s,[&](const auto& str){labels.push_back(str);});
  REQUIRE(labels.size()==2);REQUIRE(labels[0].find("entries=1 draws=1 vertices=0 indices=6 clears=0 overflow=0")!=std::string::npos);
  REQUIRE(labels[1].find("retail=31:scene-to-gbuffer")!=std::string::npos);
  REQUIRE(labels[1].find("guest_vs=0000000000000001:fixture_vs")!=std::string::npos);
  REQUIRE(labels[1].find("guest_ps=0000000000000002:fixture_ps")!=std::string::npos);
  REQUIRE(labels[1].find("pipeline=AB mixed_pipelines=0 first_list=9 last_list=9")!=std::string::npos);
  REQUIRE(s.active()); // Emission is observational; scope owner closes it.
}
TEST_CASE("Workload sized streams use bounded summary storage across repeated frames", "[graphics][gpu-labels]") {
  NativeGpuScopeSummary s;const size_t object_bytes=sizeof(s);
  for(uint32_t frame=0;frame<64;++frame){
    s.Begin(frame,0);
    for(uint32_t draw=0;draw<16384;++draw)s.Observe(Key(draw%8,2),"v","p",1,draw,0,6);
    REQUIRE(s.draws()==16384);REQUIRE(s.indices()==98304);REQUIRE(s.entries().size()==8);
    REQUIRE(s.overflow_draws()==0);REQUIRE(sizeof(s)==object_bytes);s.End();
  }
}
TEST_CASE("Independent scope streams do not share mutable state", "[graphics][gpu-labels]") {
  std::array<uint64_t,4> counts{};std::array<std::thread,4> workers;
  for(size_t i=0;i<workers.size();++i) workers[i]=std::thread([&,i]{
    NativeGpuScopeSummary s;s.Begin(uint32_t(i),0);
    for(size_t n=0;n<10000;++n)s.Observe(Key(i,2),"v","p",1,1,0,3);
    counts[i]=s.indices();s.End();
  });
  for(auto& worker:workers)worker.join();
  for(auto count:counts)REQUIRE(count==30000);
}

TEST_CASE("Scope attribution separates stock and replacement shader execution", "[graphics][gpu-labels]") {
  NativeGpuScopeSummary s;
  s.Begin(7, 0);
  auto stock = Key(1, 0x535ACDAB8AE84D82ull);
  stock.samples = 1;
  auto replacement = stock;
  replacement.shader_variant = 2;
  s.Observe(stock, "guest-vs", "guest-ps", 10, 1, 3, 0);
  s.Observe(replacement, "guest-vs", "guest-ps", 11, 2, 3, 0,
            "guest-vs", "motion_blur/gta_composite_mb_e2.glsl");
  REQUIRE(s.entries().size() == 2);
  REQUIRE(s.entries()[0].selected_pixel_name == "guest-ps");
  REQUIRE(s.entries()[1].selected_pixel_name == "motion_blur/gta_composite_mb_e2.glsl");
  std::vector<std::string> labels;
  EmitNativeGpuScopeLabels(s, [&](const auto& label) { labels.push_back(label); });
  REQUIRE(labels[2].find("guest_ps=535ACDAB8AE84D82:guest-ps") != std::string::npos);
  REQUIRE(labels[2].find("selected_ps=535ACDAB8AE84D82:motion_blur/gta_composite_mb_e2.glsl") != std::string::npos);
  REQUIRE(labels[2].find("variant=2 samples=1") != std::string::npos);
}
