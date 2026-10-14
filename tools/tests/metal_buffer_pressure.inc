// Production cache pressure and GPU lifetime regression, included after Check.
static void ExerciseBufferPressure(const std::shared_ptr<rex::ui::metal::MetalContext>& context,
                                   rex::memory::Memory& memory) {
  using namespace rex;
  using namespace rex::graphics::gta4_metal;
  auto* heap = memory.LookupHeap(0x10000000);
  uint32_t arena = 0;
  Check(heap && heap->Alloc(4096, 4096,
      memory::kMemoryAllocationReserve | memory::kMemoryAllocationCommit,
      memory::kMemoryProtectRead | memory::kMemoryProtectWrite, false, &arena), "Pressure arena allocation");
  GuestMemory guest(&memory);
  auto word = [&](uint32_t address, uint32_t value) {
    value = __builtin_bswap32(value);
    Check(guest.Write(address, {reinterpret_cast<const uint8_t*>(&value), sizeof(value)}), "Pressure write");
  };
  ResourceStore cache(context, guest, 64);
  VertexDeclaration declaration{}; declaration.identity = 1;
  declaration.elements.push_back({0, 0, 0x1A23A6, 0, 0, 0, 0});
  ShaderMetadata shader{}; shader.hash = 1; shader.attribute_count = 1;
  shader.attributes[0] = {0, 0, MetalVertexScalar::kFloat, 4};
  constexpr size_t count = 12, bytes = 32;
  auto result = [context->device newBufferWithLength:count * bytes options:MTLResourceStorageModeShared];
  auto commands = [context->queue commandBuffer];
  auto blit = [commands blitCommandEncoder];
  Check(result && commands && blit, "Pressure GPU objects");
  std::string error;
  id<MTLBuffer> recent = nil;
  for (size_t i = 0; i < count; ++i) {
    const uint32_t handle = arena + uint32_t(i * 32);
    const uint32_t data = arena + 1024 + uint32_t(i * bytes);
    word(handle, 1); word(handle + 24, data); word(handle + 28, bytes | 2);
    for (size_t j = 0; j < bytes / sizeof(float); ++j)
      word(data + uint32_t(j * sizeof(float)), std::bit_cast<uint32_t>(float(i * 10 + j)));
    auto buffer = cache.VertexBuffer(handle, declaration, shader, 0, 0, 16, error);
    Check(buffer != nil, error);
    [blit copyFromBuffer:buffer sourceOffset:0 toBuffer:result destinationOffset:i * bytes size:bytes];
    Check(cache.buffer_cache_statistics().captured_bytes <= 64, "Capture budget was exceeded");
    Check(cache.buffer_cache_statistics().entries <= 2, "Pressure retained too many captures");
    if (i == count - 1) recent = buffer;
  }
  Check(cache.buffer_cache_statistics().evictions == count - 2, "Expected bounded evictions did not occur");
  // A reused entry becomes most recent. Evicting another entry must not lose it.
  const uint32_t last = arena + uint32_t((count - 1) * 32);
  Check(cache.VertexBuffer(last, declaration, shader, 0, 0, 16, error) == recent, "Hot capture was not reused");
  auto recaptured = cache.VertexBuffer(arena, declaration, shader, 0, 0, 16, error);
  Check(recaptured && static_cast<const float*>(recaptured.contents)[7] == 7, "Evicted capture was not reconstructed");
  Check(cache.VertexBuffer(last, declaration, shader, 0, 0, 16, error) == recent, "LRU evicted the hottest capture");
  // Failed oversized admission must preserve current named captures.
  const auto before = cache.buffer_cache_statistics();
  word(arena + 512, 1); word(arena + 536, arena + 2048); word(arena + 540, 128 | 2);
  Check(cache.VertexBuffer(arena + 512, declaration, shader, 0, 0, 16, error) == nil, "Oversized capture was accepted");
  Check(cache.buffer_cache_statistics().captured_bytes == before.captured_bytes, "Failed admission mutated cache");
  // Replacements keep old encoded generations intact even before GPU submission.
  word(arena + 1024, std::bit_cast<uint32_t>(99.0f)); cache.Invalidate(arena);
  auto changed = cache.VertexBuffer(arena, declaration, shader, 0, 0, 16, error);
  Check(changed && static_cast<const float*>(changed.contents)[0] == 99, "Replacement capture is stale");
  Check(static_cast<const float*>(recaptured.contents)[0] == 0, "Old capture bytes changed");
  cache.Clear(); recent = nil; recaptured = nil; changed = nil;
  Check(cache.buffer_bytes() == 0 && cache.buffer_cache_statistics().entries == 0, "Pressure cleanup failed");
  [blit endEncoding]; [commands commit]; [commands waitUntilCompleted];
  Check(commands.status == MTLCommandBufferStatusCompleted, rex::ui::metal::MetalError(commands.error, "Evicted GPU references failed"));
  const auto* values = static_cast<const float*>(result.contents);
  for (size_t i = 0; i < count; ++i) for (size_t j = 0; j < bytes / sizeof(float); ++j)
    Check(values[i * (bytes / sizeof(float)) + j] == float(i * 10 + j), "Eviction corrupted queued GPU bytes");
  auto after_clear = cache.VertexBuffer(arena, declaration, shader, 0, 0, 16, error);
  Check(after_clear != nil, "Cache reuse after Clear failed");
  cache.Release(arena); Check(cache.buffer_bytes() == 0, "Release left cache accounting");
  heap->Release(arena);
  std::printf("metal_buffer_pressure=passed captures=%zu budget=64\n", count);
}
