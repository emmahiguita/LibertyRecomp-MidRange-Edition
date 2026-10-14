// Exact ResourceStore paths: a recurring working set larger than the old
// per-buffer limits, global byte-pressure LRU, and queued immutable generations.
static void ExerciseVertexConversionRetention(
    const std::shared_ptr<rex::ui::metal::MetalContext>& context, rex::memory::Memory& memory) {
  using namespace rex;
  using namespace rex::graphics::gta4_metal;
  auto* heap=memory.LookupHeap(0x10000000); uint32_t arena=0;
  Check(heap && heap->Alloc(4096,4096,
      memory::kMemoryAllocationReserve|memory::kMemoryAllocationCommit,
      memory::kMemoryProtectRead|memory::kMemoryProtectWrite,false,&arena),"Conversion arena allocation");
  GuestMemory guest(&memory);
  auto word=[&](uint32_t address,uint32_t value) {
    value=__builtin_bswap32(value);
    Check(guest.Write(address,{reinterpret_cast<const uint8_t*>(&value),sizeof(value)}),"Conversion metadata write");
  };
  constexpr size_t bytes=32, variants=12;
  const uint32_t payload=arena+512;
  for(uint32_t offset:{0u,64u,128u}) {
    word(arena+offset,1); word(arena+offset+24,payload); word(arena+offset+28,32|2);
  }
  std::array<uint8_t,bytes> original{};
  for(size_t i=0;i<original.size();++i) original[i]=uint8_t(i+1);
  Check(guest.Write(payload,original),"Conversion payload write");
  const std::array<uint8_t,bytes> expected{
      2,1,4,3,6,5,8,7,12,11,10,9,16,15,14,13,
      18,17,20,19,22,21,24,23,28,27,26,25,32,31,30,29};
  VertexDeclaration declaration{}; declaration.identity=1;
  declaration.elements.push_back({0,0,0x1A235A,0,0,0,0});
  ShaderMetadata shader{}; shader.hash=1; shader.attribute_count=1;
  shader.attributes[0]={0,0,MetalVertexScalar::kFloat,4};
  std::string error;
  {
    ResourceStore cache(context,guest);
    auto readback=[context->device newBufferWithLength:variants*bytes options:MTLResourceStorageModeShared];
    auto commands=[context->queue commandBuffer]; auto blit=[commands blitCommandEncoder];
    Check(readback && commands && commands.retainedReferences && blit,"Conversion GPU objects");
    @autoreleasepool {
      std::array<id<MTLBuffer>,variants> retained{};
      for(size_t i=0;i<variants;++i) {
        declaration.identity=i+1; shader.hash=i+1;
        retained[i]=cache.VertexBuffer(arena,declaration,shader,0,0,16,error);
        Check(retained[i]!=nil,error);
        Check(!std::memcmp(retained[i].contents,expected.data(),bytes),"Conversion bytes differ");
        [blit copyFromBuffer:retained[i] sourceOffset:0 toBuffer:readback destinationOffset:i*bytes size:bytes];
      }
      for(size_t i=1;i<retained.size();++i)Check(retained[i]==retained[0],"Equivalent correction plans allocated duplicate buffers");
      Check(cache.vertex_conversion_statistics().misses==1,"Correction-plan aliases missed their shared generation");
      // Cycling the whole working set must hit; a larger but still arbitrary
      // eight-entry FIFO/LRU would fail on the first revisited identity.
      for(size_t cycle=0;cycle<4;++cycle) for(size_t i=0;i<variants;++i) {
        declaration.identity=i+1; shader.hash=i+1;
        Check(cache.VertexBuffer(arena,declaration,shader,0,0,16,error)==retained[i],
              "Recurring conversion working set thrashed");
      }
      auto modified=original; modified[0]=0xE1;
      Check(guest.Write(payload,modified),"Conversion guest mutation"); cache.Invalidate(arena);
      declaration.identity=1; shader.hash=1;
      auto changed=cache.VertexBuffer(arena,declaration,shader,0,0,16,error);
      Check(changed && changed!=retained[0],"Guest write reused an old generation");
      Check(static_cast<const uint8_t*>(changed.contents)[1]==0xE1,"Changed generation is stale");
      Check(!std::memcmp(retained[0].contents,expected.data(),bytes),"Guest write mutated an encoded generation");
      cache.Clear(); retained.fill(nil); changed=nil;
      Check(cache.buffer_bytes()==0,"Clear retained conversion accounting");
    }
    // The inner pool has drained all temporary CPU owners before submission.
    [blit endEncoding]; [commands commit]; [commands waitUntilCompleted];
    Check(commands.status==MTLCommandBufferStatusCompleted,
        ui::metal::MetalError(commands.error,"Conversion generations failed on GPU"));
    for(size_t i=0;i<variants;++i)
      Check(!std::memcmp(static_cast<const uint8_t*>(readback.contents)+i*bytes,expected.data(),bytes),
            "Release-before-submit changed GPU conversion bytes");
  }
  Check(guest.Write(payload,original),"Restore conversion payload");
  {
    auto probe=[context->device newBufferWithLength:bytes
        options:MTLResourceStorageModeShared|MTLResourceCPUCacheModeDefaultCache];
    Check(probe!=nil,"Conversion budget probe allocation");
    // Production charges 256 bytes for the map/list nodes in addition to the
    // driver's allocation size. Choose a deterministic two-entry byte budget.
    const size_t charge=probe.allocatedSize+256+sizeof(uint64_t);
    ResourceStore cache(context,guest,ResourceStore::kDefaultBufferCaptureBudget,SIZE_MAX,charge*2);
    declaration.identity=1; shader.hash=1;
    auto readback=[context->device newBufferWithLength:96 options:MTLResourceStorageModeShared];
    auto commands=[context->queue commandBuffer]; auto blit=[commands blitCommandEncoder];
    Check(readback && commands && commands.retainedReferences && blit,"Pressure GPU objects");
    @autoreleasepool {
      auto first=cache.VertexBuffer(arena,declaration,shader,0,0,16,error);
      auto second=cache.VertexBuffer(arena+64,declaration,shader,0,0,16,error);
      Check(first && second,"Conversion pressure fill failed");
      [blit copyFromBuffer:first sourceOffset:0 toBuffer:readback destinationOffset:0 size:bytes];
      [blit copyFromBuffer:second sourceOffset:0 toBuffer:readback destinationOffset:32 size:bytes];
      Check(cache.VertexBuffer(arena,declaration,shader,0,0,16,error)==first,"Pressure cache hit failed");
      auto third=cache.VertexBuffer(arena+128,declaration,shader,0,0,16,error);
      Check(third!=nil,error);
      [blit copyFromBuffer:third sourceOffset:0 toBuffer:readback destinationOffset:64 size:bytes];
      Check(cache.VertexBuffer(arena,declaration,shader,0,0,16,error)==first,"Global LRU evicted hot conversion");
      Check(cache.VertexBuffer(arena+128,declaration,shader,0,0,16,error)==third,"Newest conversion was not retained");
      auto recreated=cache.VertexBuffer(arena+64,declaration,shader,0,0,16,error);
      Check(recreated && recreated!=second,"Byte budget did not evict the oldest conversion");
      Check(!std::memcmp(second.contents,expected.data(),bytes),"Eviction modified retained generation");
      cache.Release(arena+64); cache.Release(arena); cache.Release(arena+128);
      Check(cache.buffer_bytes()==0,"Release left global conversion accounting");
      Check(cache.VertexBuffer(arena,declaration,shader,0,0,16,error)!=nil,"Cache unusable after release");
      cache.Clear(); Check(cache.buffer_bytes()==0,"Pressure clear left accounting");
      Check(cache.VertexBuffer(arena,declaration,shader,0,0,16,error)!=nil,"Cache unusable after clear");
      cache.Clear(); first=nil; second=nil; third=nil; recreated=nil;
    }
    [blit endEncoding]; [commands commit]; [commands waitUntilCompleted];
    Check(commands.status==MTLCommandBufferStatusCompleted,
        ui::metal::MetalError(commands.error,"Evicted conversions failed on GPU"));
    for(size_t offset:{0u,32u,64u})
      Check(!std::memcmp(static_cast<const uint8_t*>(readback.contents)+offset,expected.data(),bytes),
            "Byte-pressure eviction changed queued GPU bytes");
  }
  {
    ResourceStore uncached(context,guest,ResourceStore::kDefaultBufferCaptureBudget,SIZE_MAX,0);
    auto first=uncached.VertexBuffer(arena,declaration,shader,0,0,16,error);
    auto second=uncached.VertexBuffer(arena,declaration,shader,0,0,16,error);
    Check(first && second && first!=second,"Zero retention budget did not bypass cache");
    Check(uncached.buffer_bytes()==bytes,"Uncached conversion was charged as retained");
    Check(!std::memcmp(first.contents,expected.data(),bytes),"Uncached conversion changed contents");
  }
  heap->Release(arena);
  std::printf("metal_vertex_conversion_retention=passed recurring_variants=%zu pressure_lru=true immutable_gpu=true\n",variants);
}
