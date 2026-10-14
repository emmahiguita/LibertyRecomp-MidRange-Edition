#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <rex/graphics/gta4_native/temporal_commands.h>
#include "graphics/gta4_metal/temporal/composite_contract.h"
#include "graphics/gta4_metal/temporal/draw_policy.h"
#include "graphics/gta4_metal/temporal/geometry_history.h"
#include "graphics/gta4_metal/temporal/title_boundary.h"

namespace {
using namespace rex::graphics::gta4_native;
namespace temporal=rex::graphics::gta4_metal::temporal;
unsigned checks=0;
#define CHECK(condition) do { ++checks; if(!(condition)) { \
  std::fprintf(stderr,"temporal draw policy failed at line %d: %s\n",__LINE__,#condition); \
  std::exit(1); } } while(false)

void GeometryBindings() {
  temporal::GeometryKey base{1,2,3,4,5,6,7};
  base.streams[0]={9,16,24};base.streams[3]={10,32,48};
  base.index_generation=11;base.pixel_shader=12;base.base_vertex=-2;
  base.primitive=6;base.indexed=true;base.restart=true;base.restart_index=65535;
  std::array<uint8_t,4096> vertex{};
  std::array<uint8_t,sizeof(core::SharedConstants)> shared{};
  const auto check_change=[&](auto change){
    temporal::GeometryHistory history;
    CHECK(history.Begin(1,1,1,false));
    CHECK(history.Record(base,vertex,shared,{}).captured);
    CHECK(history.Begin(1,1,2,false));
    auto changed=base;change(changed);
    CHECK(changed!=base);
    CHECK(!history.Record(changed,vertex,shared,{}).previous);
    CHECK(history.Record(base,vertex,shared,{}).previous);
  };
  check_change([](auto& k){k.streams[0].offset=32;});
  check_change([](auto& k){k.streams[0].stride=48;});
  check_change([](auto& k){k.streams[3].offset=0;});
  check_change([](auto& k){k.streams[3].generation=21;});
  check_change([](auto& k){k.index_generation=22;});
  check_change([](auto& k){k.base_vertex=0;});
  check_change([](auto& k){k.pixel_shader=23;});
  check_change([](auto& k){k.layout=24;});
  check_change([](auto& k){k.range=25;});
  check_change([](auto& k){k.primitive=4;});
  check_change([](auto& k){k.indexed=false;});
  check_change([](auto& k){k.restart=false;});
  check_change([](auto& k){k.restart_index=65534;});
  temporal::GeometryHistory history;
  CHECK(history.Begin(1,1,1,false));
  CHECK(history.Record(base,vertex,shared,{}).captured);
  CHECK(history.Begin(1,1,2,false));
  CHECK(history.Record(base,vertex,shared,{}).previous);
  vertex[0]=1;
  CHECK(history.Record(base,vertex,shared,{}).ambiguous);
  CHECK(history.Begin(1,1,3,false));
  CHECK(!history.Record(base,vertex,shared,{}).previous);
}
void MaterialSampling() {
  temporal::Configuration c{1920,1080,3840,2160,temporal::Method::kMetalFx,false};
  CHECK(temporal::MaterialMipBias(c,true)==-2.0f);
  CHECK(temporal::MaterialMipBias(c,false)==0.0f);
  c.input_width=2560;c.input_height=1440;
  CHECK(std::abs(temporal::MaterialMipBias(c,true)-(-1.5849625f))<0.00001f);
  c.input_width=c.output_width;c.input_height=c.output_height;
  CHECK(temporal::MaterialMipBias(c,true)==0.0f);
  c.method=temporal::Method::kTaa;
  CHECK(temporal::MaterialMipBias(c,true)==0.0f);
  c.input_width=0;
  CHECK(temporal::MaterialMipBias(c,true)==0.0f);
}

void CompositeScratch() {
  core::FixedFunctionState guest{};
  guest.depth_enable=guest.depth_write_enable=guest.stencil_enable=1;
  guest.alpha_test_enable=guest.alpha_to_mask_enable=guest.alpha_to_mask=1;
  guest.blend_enable=1;guest.blend_controls.fill(0x00070006u);
  guest.color_write_mask=0x1u;guest.cull_mode=2;guest.polygon_mode=1;
  guest.scissor={4,6,1200,700};guest.scissor_enable=1;
  guest.viewport_bits={std::bit_cast<uint32_t>(2.0f),std::bit_cast<uint32_t>(3.0f),
      std::bit_cast<uint32_t>(1280.0f),std::bit_cast<uint32_t>(720.0f),
      std::bit_cast<uint32_t>(0.0f),std::bit_cast<uint32_t>(1.0f)};
  const auto preserved=guest;
  const auto scratch=temporal::LinearCompositeState(guest);
  CHECK(guest==preserved);
  CHECK(!scratch.depth_enable&&!scratch.depth_write_enable&&!scratch.stencil_enable);
  CHECK(!scratch.alpha_test_enable&&!scratch.alpha_to_mask_enable&&!scratch.alpha_to_mask);
  CHECK(!scratch.blend_enable&&scratch.color_write_mask==0xFu);
  for(auto blend:scratch.blend_controls)CHECK(!IsNativeBlendControlEnabled(blend));
  CHECK(scratch.viewport_bits==guest.viewport_bits&&scratch.scissor==guest.scissor);
  CHECK(scratch.cull_mode==guest.cull_mode&&scratch.polygon_mode==guest.polygon_mode);
  const auto f6=temporal::CompositeForShader(0xF6AEB9A606561C54ull);
  CHECK(f6&&f6->scene_stage==1&&f6->filter==temporal::CompositeFilter::kStockF6);
  CHECK(!SupportsSplitPostFx(0xF6AEB9A606561C54ull));
  CHECK(!temporal::CompositeForShader(0));
  CHECK(temporal::FiniteViewport(guest)&&temporal::RasterHasArea(guest,1280,720));
  guest.scissor[2]=guest.scissor[0];CHECK(!temporal::RasterHasArea(guest,1280,720));
  guest=preserved;guest.viewport_bits[2]=std::bit_cast<uint32_t>(0.0f);
  CHECK(!temporal::RasterHasArea(guest,1280,720));
  guest=preserved;guest.viewport_bits[2]=std::bit_cast<uint32_t>(std::numeric_limits<float>::infinity());
  CHECK(!temporal::FiniteViewport(guest));
}

void ExecutionAndJitter() {
  LightingContext lighting{};
  auto primary=temporal::ClassifySceneDraw(true,false,false,true,false,false,true,lighting);
  CHECK(primary.primary&&primary.world&&primary.motion_relevant&&!primary.effect);
  auto unclassified=temporal::ClassifySceneDraw(true,false,false,false,false,false,true,lighting);
  CHECK(!unclassified.world&&unclassified.motion_relevant);
  CHECK(!temporal::ClassifySceneDraw(true,false,false,true,true,true,true,lighting).world);
  lighting.stage=RenderExecutionStage::kDeferredLighting;
  auto light=temporal::ClassifySceneDraw(true,false,false,true,true,false,true,lighting);
  CHECK(!light.world&&!light.motion_relevant);
  lighting.role=LightPassRole::kCorona;
  auto corona=temporal::ClassifySceneDraw(true,false,false,false,false,true,true,lighting);
  CHECK(corona.effect&&corona.motion_relevant&&!corona.world);
  CHECK(!temporal::ClassifySceneDraw(false,false,false,true,true,false,true,lighting).motion_relevant);
  CHECK(!temporal::ClassifySceneDraw(true,true,false,true,true,false,true,lighting).motion_relevant);
  lighting={};
  CHECK(temporal::ClassifySceneDraw(true,false,false,false,true,false,true,lighting).world);
  CHECK(!temporal::ClassifySceneDraw(true,false,false,false,true,false,false,lighting).world);
  bool decided=false,eligible=false;
  temporal::CommitPrimaryJitter(false,true,false,true,decided,eligible);
  CHECK(!decided);
  CHECK(!temporal::AdmitJitter(decided,eligible,true,true,false));
  temporal::CommitPrimaryJitter(true,true,false,true,decided,eligible);
  CHECK(decided&&!eligible);
  temporal::CommitPrimaryJitter(true,true,true,true,decided,eligible);
  CHECK(!eligible&&!temporal::AdmitJitter(decided,eligible,true,true,true));
  decided=eligible=false;
  CHECK(temporal::AdmitJitter(decided,eligible,true,true,true));
  temporal::CommitPrimaryJitter(true,true,true,true,decided,eligible);
  CHECK(temporal::AdmitJitter(decided,eligible,true,true,false));
  CHECK(!temporal::AdmitJitter(decided,eligible,true,false,true));
  core::SharedConstants shared{};shared.modern_effects.depth_range[1]=1;
  CHECK(temporal::StandardDepthViewport(shared));
  shared.modern_effects.depth_range[0]=0.25f;CHECK(!temporal::StandardDepthViewport(shared));
}

void Provenance() {
  CHECK(TemporalExecutionFlags(false,31)==0);
  for(auto phase:{14u,15u,16u,31u})CHECK(TemporalExecutionFlags(true,phase)&kTemporalSceneGeometry);
  for(auto phase:{1u,2u,3u,9u,10u,13u,17u,19u,20u,21u,23u,24u,32u,34u,35u,36u}){
    CHECK(TemporalExecutionFlags(true,phase)&kTemporalScreenSpace);
    CHECK(!(TemporalExecutionFlags(true,phase)&kTemporalSceneGeometry));
  }
  CHECK(TemporalExecutionFlags(true,999)==kTemporalExecutionAttributed);
  LightingContext lighting{};lighting.stage=RenderExecutionStage::kCompositePostFx;
  lighting.source_function=0x822D1710;lighting.occurrence_id=17;
  CHECK(TemporalCompositeExecutionFlags(lighting,false)==kTemporalCompositeExecution);
  CHECK(TemporalCompositeExecutionFlags(lighting,true)==(kTemporalCompositeExecution|kTemporalFinalCompositeExecution));
  lighting.source_function=0;CHECK(TemporalCompositeExecutionFlags(lighting,true)==0);
  lighting.source_function=0x822D1710;lighting.occurrence_id=0;CHECK(TemporalCompositeExecutionFlags(lighting,true)==0);
  lighting.occurrence_id=17;lighting.stage=RenderExecutionStage::kSceneToGBuffer;
  CHECK(TemporalCompositeExecutionFlags(lighting,true)==0);
  TemporalCommand a{};a.device=1;a.view=2;a.sequence=3;a.width=1280;a.height=720;
  a.output_width=1920;a.output_height=1080;
  auto b=a;b.time_ns=99;b.projection[0]=2;
  CHECK(SameTemporalScene(a,b));
  b.output_width=1280;CHECK(!SameTemporalScene(a,b));
  b=a;b.sequence=4;CHECK(!SameTemporalScene(a,b));
  b=a;b.view=9;CHECK(!SameTemporalScene(a,b));
}
void DeclaredBoundaries() {
  using namespace gta4::temporal_boundary;
  CHECK(DeclaresComposite(0x1000,0));
  CHECK(!DeclaresComposite(0,1));
  CHECK(!DeclaresComposite(0x1000,UINT32_MAX));
  Registry registry;
  CHECK(!registry.Find(1,1,11));
  registry.GBuffer(1,1,11,21);
  CHECK(!registry.Find(1,1,11));
  registry.Composite(1,1,21,31,true);
  CHECK(registry.Find(1,1,11).composite_phase==31);
  CHECK(!registry.Find(1,2,11));
  CHECK(!registry.Find(2,1,11));
  CHECK(!registry.Find(1,1,12));
  registry.Composite(1,2,21,31,true);
  CHECK(!registry.Find(1,2,11));
  registry.GBuffer(1,2,11,21);
  CHECK(registry.Find(1,2,11).composite_phase==31);
  registry.GBuffer(1,3,11,21);
  registry.Composite(1,3,21,31,false);
  CHECK(!registry.Find(1,3,11));
  registry.Composite(1,3,21,31,true);
  CHECK(!registry.Find(1,3,11));
  registry.GBuffer(1,4,11,21);
  registry.Composite(1,4,21,31,true);
  registry.Composite(1,4,21,32,true);
  CHECK(!registry.Find(1,4,11));
  registry.GBuffer(1,5,11,21);
  registry.Composite(1,5,21,31,true);
  registry.GBuffer(1,5,11,22);
  CHECK(!registry.Find(1,5,11));
  registry.GBuffer(1,6,11,21);
  registry.Composite(1,6,22,31,true);
  CHECK(!registry.Find(1,6,11));
  registry.Composite(1,6,21,32,true);
  CHECK(registry.Find(1,6,11).composite_phase==32);
  Registry full;
  for(uint32_t i=1;i<=129;++i){full.GBuffer(1,1,i,i);full.Composite(1,1,i,i,true);}
  CHECK(!full.Find(1,1,1));
  // An old worker observation may remove admission, but never borrows the
  // newer frame's boundary for an older frame or vice versa.
  registry.Composite(1,5,21,31,true);
  CHECK(!registry.Find(1,6,11));
  CHECK(!registry.Find(1,5,11));
}
}  // namespace
int main(){GeometryBindings();MaterialSampling();CompositeScratch();ExecutionAndJitter();Provenance();DeclaredBoundaries();
  std::printf("temporal draw policy: %u checks passed\n",checks);return 0;}
