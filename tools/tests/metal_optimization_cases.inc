void ExerciseResolveContinuity(Fixture& f, uint32_t samples, bool tiled) {
  ConfigureGeometryFixture(f);
  const auto source = f.Surface(false, samples), other = f.Surface(false, samples);
  const auto snapshot = f.MakeTexture(false, tiled, xenos::Endian::k8in32);
  f.Bind(source, {}); f.Clear(1, {1, 0, 0, 1}, 0, 0);
  const auto resolve = f.ResolveRequest(source, snapshot, false); f.Submit(resolve);
  f.Bind(other, {});
  using namespace rex::graphics::gta4_native::core;
  f.Word(f.arena + kDepthEnableOffset, 0); f.Word(f.arena + kDepthStencilPackedOffset, 0);
  f.Word(f.arena + kPackedScissorTopLeftOffset, 0);
  const std::array<GeometryVertex, 4> quad{GeometryCorner(-1, 1), GeometryCorner(1, 1),
      GeometryCorner(1, -1), GeometryCorner(-1, -1)};
  SubmitGeometry(f, quad, uint32_t(xenos::PrimitiveType::kQuadList), {1, 1, 1, 1});
  const auto before = f.renderer.statistics();
  f.Submit(resolve);
  SubmitGeometry(f, quad, uint32_t(xenos::PrimitiveType::kQuadList), {1, 1, 1, 1});
  const auto after = f.renderer.statistics();
  Check(after.resolve_skips == before.resolve_skips + 1, "Continuity fixture did not hit the resolve cache");
  Check(after.render_passes_created == before.render_passes_created, "Reused resolve broke compatible draws into separate passes");
  for (auto pixel : f.Read(snapshot)) Check(pixel == 0xFF0000FFu, "Preserved encoder changed the resolved source");
  const auto output = f.MakeTexture(false, tiled, xenos::Endian::k8in32);
  CheckGeometryPixels(f, other, output, {1, 1, 1, 1}, "resolve continuity");
  std::printf("resolve_continuity samples=%u tiled=%u passed=true\n", 1u << samples, unsigned(tiled));
}

// Exact production-path A/B cases. Only optimization policy differs; shaders,
// source values, geometry, formats and all fixed-function inputs are identical.
std::vector<uint32_t> OptimizationSequence(Fixture& f,uint32_t samples,bool tiled,bool optimized) {
  ConfigureGeometryFixture(f);
  using namespace rex::graphics::gta4_native::core;
  const auto color=f.Surface(false,samples),depth=f.Surface(true,samples);
  const auto color_texture=f.MakeTexture(false,tiled,xenos::Endian::k8in32);
  const auto depth_texture=f.MakeTexture(true,tiled,xenos::Endian::k8in32);
  const std::array<GeometryVertex,4> quad{GeometryCorner(-1,1),GeometryCorner(1,1),
      GeometryCorner(1,-1),GeometryCorner(-1,-1)};
  const std::array<std::array<float,4>,3> values{{{1,0.5f,0.25f,1},{0.5f,0.25f,1,1},{1,0.5f,0.25f,1}}};
  std::vector<uint32_t> result;
  for(uint32_t variant=0;variant<20;++variant) {
    f.Bind(color,depth);
    f.Word(f.arena+kDepthEnableOffset,1);
    f.Word(f.arena+kDepthStencilPackedOffset,(7u<<4)|4u);
    f.Word(f.arena+kPackedColorWriteMaskOffset,0xF);
    f.Word(f.arena+kPackedScissorTopLeftOffset,0);
    f.Word(f.arena+kPackedScissorBottomRightOffset,(16u<<16)|16u);
    f.Clear(1,{0.125f,0.25f,0.5f,1},0,0);
    f.Clear(variant%2?0x10u:0x20u,{},0.75,0x31u+variant);
    f.Clear(variant%2?0x20u:0x10u,{},0.625,0x41u+variant);
    if(variant%3==0) f.Clear(1,{0.25f,0.125f,0.75f,1},0,0);
    if(variant%4==0) f.Clear(0x31,{0.5f,0.5f,0.125f,1},0.25,0x77,{4,4,12,12});
    if(variant%5!=0) {
      // Preserve an exterior guard region, exercise shared-state changes and
      // consecutive identical draw state with different immutable UP buffers.
      f.Word(f.arena+kPackedScissorTopLeftOffset,(4u<<16)|4u);
      f.Word(f.arena+kPackedScissorBottomRightOffset,(12u<<16)|12u);
      for(const auto& value:values) {
        SubmitGeometry(f,quad,uint32_t(xenos::PrimitiveType::kQuadList),value);
        SubmitGeometry(f,quad,uint32_t(xenos::PrimitiveType::kQuadList),value);
      }
      f.Word(f.arena+kPackedColorWriteMaskOffset,0);
      SubmitGeometry(f,quad,uint32_t(xenos::PrimitiveType::kQuadList),{0,0,0,0});
      f.Word(f.arena+kPackedColorWriteMaskOffset,0xF);
    }
    // Readbacks force publication of deferred clears even with no draw.
    f.Submit(f.ResolveRequest(color,color_texture,false));
    f.Submit(f.ResolveRequest(depth,depth_texture,true));
    auto colors=f.Read(color_texture),depths=f.Read(depth_texture);
    result.insert(result.end(),colors.begin(),colors.end());
    result.insert(result.end(),depths.begin(),depths.end());
  }
  const auto statistics=f.renderer.statistics();
  if(optimized) {
    Check(statistics.clear_load_folds>0,"No full clear was folded in the production sequence");
    Check(statistics.pipeline_lookup_hits>0,"Exact pipeline memo had no production-path hits");
    Check(statistics.state_skips>0 && statistics.residency_skips>0,"Production bindings never reused");
  } else Check(!statistics.clear_load_folds && !statistics.state_skips && !statistics.residency_skips && !statistics.pipeline_lookup_hits,
                "Reference path unexpectedly enabled optimizations");
  std::printf("optimization_sequence samples=%u tiled=%u enabled=%u folds=%llu materialized=%llu state_calls=%llu state_skips=%llu residency_skips=%llu\n",
      1u<<samples,unsigned(tiled),unsigned(optimized),statistics.clear_load_folds,statistics.clear_materializations,
      statistics.state_calls,statistics.state_skips,statistics.residency_skips);
  return result;
}
void ExerciseOptimizations(memory::Memory& memory,const std::shared_ptr<ui::metal::MetalContext>& context,
                           const std::filesystem::path& shaders) {
  constexpr const char* policies[]={"gta4_metal_fold_full_clears","gta4_metal_cache_encoder_state","gta4_metal_cache_pipeline_lookup","gta4_metal_cache_constant_sources", "gta4_metal_track_guest_constants", "gta4_metal_async_pipelines", "gta4_metal_prepare_textures"};
  std::array<std::string,std::size(policies)> saved{};
  for(size_t i=0;i<saved.size();++i)saved[i]=cvar::GetFlagByName(policies[i]);
  for(uint32_t samples=0;samples<3;++samples)for(bool tiled:{false,true}) {
    Check(cvar::SetFlagByName("gta4_native_anti_aliasing",samples==0?"off":samples==1?"msaa2x":"msaa4x"),"Optimization sample policy");
    std::vector<uint32_t> reference;
    for(bool optimized:{false,true}) {
      for(auto policy:policies)Check(cvar::SetFlagByName(policy,optimized?"true":"false"),"Optimization toggle missing");
      Fixture fixture(memory,context,shaders);
      auto pixels=OptimizationSequence(fixture,samples,tiled,optimized);
      if(!optimized)reference=std::move(pixels);
      else {
        Check(reference.size()==pixels.size(),"Optimization result extent changed");
        for(size_t i=0;i<pixels.size();++i)Check(reference[i]==pixels[i],"Optimization changed color/depth/stencil bits at "+std::to_string(i));
        ExerciseResolveContinuity(fixture, samples, tiled);
      }
    }
  }
  for(size_t i=0;i<saved.size();++i)Check(cvar::SetFlagByName(policies[i],saved[i]),"Restore optimization flags");
  std::puts("optimization_gpu_differential=passed bit_identical=true");
}
