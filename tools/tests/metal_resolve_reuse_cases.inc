// Actual production resolve calls, including a clear side effect on a reused
// destination. Readbacks happen only after the reuse/invalidations were tested.
void ExerciseResolveReuse(Fixture& f, uint32_t samples, bool tiled) {
  auto source=f.Surface(false,samples);f.Bind(source,{});f.Clear(1,{1,0,0,1},0,0);
  auto destination=f.MakeTexture(false,tiled,xenos::Endian::k8in32);
  auto request=f.ResolveRequest(source,destination,false);
  auto start=f.renderer.statistics();f.Submit(request);f.Submit(request);
  Check(f.renderer.statistics().resolve_skips==start.resolve_skips+1,"exact repeated resolve was not reused");
  auto clear=request;clear.flags|=0x100u;
  for (size_t i=0;i<4;++i) clear.clear_color_bits[i]=std::bit_cast<uint32_t>((i==1||i==3)?1.0f:0.0f);
  f.Submit(clear);
  Check(f.renderer.statistics().resolve_skips==start.resolve_skips+2,"clear side effect prevented safe write reuse");
  f.Submit(request);
  Check(f.renderer.statistics().resolve_skips==start.resolve_skips+2,"changed source reused an old resolve");
  for(auto pixel:f.Read(destination))Check(pixel==0xFF00FF00u,"resolve clear side effect was lost");
  auto before=f.renderer.statistics();f.Submit(request);
  Check(f.renderer.statistics().resolve_skips==before.resolve_skips,"another command recording reused an old proof");
  f.Submit(request);Check(f.renderer.statistics().resolve_skips==before.resolve_skips+1,"same new recording failed to reuse");
  auto partial=f.MakeTexture(false,tiled,xenos::Endian::k8in32);
  auto small=f.ResolveRequest(source,partial,false);small.source_rectangle_valid=1;
  small.source_rectangle={4,4,12,12};small.destination_point_valid=1;small.destination_point={4,4};
  before=f.renderer.statistics();f.Submit(small);
  const auto merged=f.renderer.statistics().resolve_initializations_merged-before.resolve_initializations_merged;
  Check(merged==(samples?1u:0u),"first partial conversion did not merge initialization correctly");
  f.Submit(small);
  Check(f.renderer.statistics().resolve_skips==before.resolve_skips+1,"partial resolve repeat was not reused");
  const auto pixels=f.Read(partial);
  for(size_t y=0;y<16;++y)for(size_t x=0;x<16;++x)
    Check(pixels[y*16+x]==((x>=4&&x<12&&y>=4&&y<12)?0xFF00FF00u:0u),"partial destination guard/clear differs");
  // A changed destination rectangle must not reuse the previous subset.
  small.destination_point={0,0};before=f.renderer.statistics();f.Submit(small);
  Check(f.renderer.statistics().resolve_skips==before.resolve_skips,"different destination region reused");
  Check(f.renderer.Finish(f.error),f.error);
  std::printf("resolve_reuse samples=%u tiled=%u passed=true\n",1u<<samples,unsigned(tiled));
}
