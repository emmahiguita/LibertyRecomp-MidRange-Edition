// Exercise production admission, reconstruction and queued GPU ownership under
// a tiny device-measured budget; a full cache must not turn into missing draws.
static void ExerciseTexturePressure(const std::shared_ptr<rex::ui::metal::MetalContext>& context,
                                    rex::memory::Memory& memory) {
  using namespace rex;
  using namespace rex::graphics;
  using namespace rex::graphics::gta4_metal;
  GuestMemory guest(&memory);
  auto* heap = memory.GetPhysicalHeap();
  uint32_t address = 0;
  Check(heap && heap->Alloc(4096, 4096,
      memory::kMemoryAllocationReserve | memory::kMemoryAllocationCommit,
      memory::kMemoryProtectRead | memory::kMemoryProtectWrite, false, &address),
      "Texture pressure guest allocation");
  std::array<uint8_t, 4096> payload{};
  xenos::xe_gpu_texture_fetch_t fetch{};
  fetch.type = xenos::FetchConstantType::kTexture;
  fetch.format = xenos::TextureFormat::k_8_8_8_8;
  fetch.dimension = xenos::DataDimension::k2DOrStacked;
  fetch.base_address = address >> 12;
  fetch.pitch = 2;
  fetch.size_2d.width = 15; fetch.size_2d.height = 7;
  fetch.swizzle = xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA;
  auto commands = [context->queue commandBuffer];
  Check(commands != nil, "Texture pressure command buffer");
  std::string error;
  auto load = [&](ResourceStore& cache, uint32_t handle, uint8_t value) {
    payload.fill(value);
    Check(guest.Write(address, payload, true), "Texture pressure payload write");
    auto texture = cache.Texture(handle, fetch, commands, [] {}, error);
    Check(bool(texture), error);
    return texture;
  };
  struct Readback { id<MTLBuffer> buffer; uint8_t expected; };
  std::vector<Readback> readbacks;
  auto record = [&](id<MTLTexture> image, uint8_t expected) {
    auto buffer = [context->device newBufferWithLength:2048 options:MTLResourceStorageModeShared];
    auto blit = [commands blitCommandEncoder];
    Check(buffer && blit, "Texture pressure readback allocation");
    [blit copyFromTexture:image sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
        sourceSize:MTLSizeMake(16, 8, 1) toBuffer:buffer destinationOffset:0
        destinationBytesPerRow:256 destinationBytesPerImage:2048];
    [blit endEncoding];
    readbacks.push_back({buffer, expected});
  };

  // Query actual allocations so this also works on devices with different
  // texture page sizes. Probe resources never become part of the test cache.
  ResourceStore probe(context, guest);
  auto measured = load(probe, 1, 0);
  const size_t unit = measured->image.allocatedSize;
  Check(unit != 0, "Texture pressure allocation size unavailable");
  gta4_native::SurfaceDescriptor surface{};
  surface.handle = 100; surface.width = 16; surface.height = 8;
  surface.base = 16; surface.format = 0x18280186;
  auto measured_surface = probe.Surface(surface, false, error);
  Check(bool(measured_surface), error);
  gta4_native::ResolveCommand resolve{};
  resolve.destination_texture = 101;
  std::memcpy(resolve.destination_fetch, &fetch, sizeof(fetch));
  auto measured_target = probe.ResolveTarget(resolve, error);
  Check(bool(measured_target), error);
  const size_t pinned_bytes = measured_surface->image.allocatedSize + measured_target->image.allocatedSize;
  probe.Clear(); measured.reset(); measured_surface.reset(); measured_target.reset();

  constexpr uint32_t count = 12;
  ResourceStore cache(context, guest, ResourceStore::kDefaultBufferCaptureBudget, unit * 3);
  for (uint32_t handle = 1; handle <= count; ++handle) {
    auto texture = load(cache, handle, uint8_t(handle));
    record(texture->image, uint8_t(handle));
    const auto stats = cache.texture_cache_statistics();
    Check(stats.bytes <= stats.budget && stats.entries <= 3, "Texture cache exceeded its budget");
  }
  Check(cache.texture_cache_statistics().evictions == count - 3, "Texture pressure did not evict assets");
  auto hot = cache.Texture(11, fetch, commands, [] {}, error);
  Check(bool(hot), error);
  const auto hot_generation = hot->generation; hot.reset();
  auto reloaded = load(cache, 1, 1); record(reloaded->image, 1); reloaded.reset();
  Check(!cache.FindTexture(10), "Texture LRU kept the coldest asset");
  Check(cache.FindTexture(11) && cache.FindTexture(11)->generation == hot_generation,
        "Texture LRU evicted a recently sampled asset");

  // Replacing a name at capacity needs only the new generation's cache space.
  const auto old_generation = cache.FindTexture(12)->generation;
  const auto before_replace = cache.texture_cache_statistics();
  cache.Invalidate(12);
  auto replaced = load(cache, 12, 99); record(replaced->image, 99);
  Check(replaced->generation != old_generation, "Dirty asset reused an old generation");
  Check(cache.texture_cache_statistics().evictions == before_replace.evictions &&
        cache.texture_bytes() == before_replace.bytes, "Same-size replacement evicted unrelated assets");
  replaced.reset();
  auto oversized = fetch; oversized.size_2d.width = 4095; oversized.size_2d.height = 4095;
  const auto before_failure = cache.texture_cache_statistics();
  Check(!cache.Texture(12, oversized, commands, [] {}, error), "Oversized texture was admitted");
  Check(error.find("allocation exceeds budget") != std::string::npos, "Missing oversized admission reason");
  Check(cache.texture_bytes() == before_failure.bytes &&
        cache.texture_cache_statistics().evictions == before_failure.evictions && cache.FindTexture(12),
        "Failed replacement mutated named cache ownership");
  cache.Clear();
  Check(cache.texture_bytes() == 0 && cache.texture_cache_statistics().entries == 0,
        "Texture pressure Clear left cache ownership");
  auto after_clear = load(cache, 1, 77); record(after_clear->image, 77); after_clear.reset();
  cache.Release(1); cache.Release(1);
  Check(cache.texture_bytes() == 0, "Texture Release left cache accounting");

  // Surfaces and GPU-produced resolves stay pinned while CPU assets churn.
  ResourceStore mixed(context, guest, ResourceStore::kDefaultBufferCaptureBudget, pinned_bytes + unit);
  auto render_surface = mixed.Surface(surface, false, error); Check(bool(render_surface), error);
  auto target = mixed.ResolveTarget(resolve, error); Check(bool(target), error);
  load(mixed, 1, 1); load(mixed, 2, 2);
  Check(!mixed.FindTexture(1) && mixed.FindTexture(2) && mixed.FindTexture(101) == target &&
        mixed.FindSurface(100) == render_surface, "Pressure evicted GPU-only content");
  Check(mixed.texture_cache_statistics().evictions == 1, "Mixed cache did not reclaim its asset");
  mixed.Clear(); target.reset(); render_surface.reset();

  ResourceStore pinned(context, guest, ResourceStore::kDefaultBufferCaptureBudget, pinned_bytes);
  render_surface = pinned.Surface(surface, false, error); Check(bool(render_surface), error);
  target = pinned.ResolveTarget(resolve, error); Check(bool(target), error);
  Check(!pinned.Texture(1, fetch, commands, [] {}, error), "Pinned cache accepted an over-budget asset");
  Check(error.find("insufficient reloadable textures") != std::string::npos &&
        pinned.texture_bytes() == pinned_bytes && pinned.FindTexture(101) == target &&
        pinned.FindSurface(100) == render_surface, "Pinned failure destroyed GPU content");
  // A changed placement replaces the surface with the same physical size.
  surface.address = 1;
  auto replacement_surface = pinned.Surface(surface, false, error);
  Check(replacement_surface && replacement_surface != render_surface &&
        pinned.texture_bytes() == pinned_bytes, "Full-cache surface replacement failed");
  Check(pinned.texture_cache_statistics().evictions == 0 &&
        pinned.texture_cache_statistics().rejections == 1, "Pinned pressure counters incorrect");
  pinned.Clear(); target.reset(); render_surface.reset(); replacement_surface.reset();

  // Every cache generation has been dropped before submission. Metal must
  // retain the exact images and staging data referenced by these encoded blits.
  [commands commit]; [commands waitUntilCompleted];
  Check(commands.status == MTLCommandBufferStatusCompleted,
        ui::metal::MetalError(commands.error, "Evicted texture GPU references failed"));
  for (const auto& readback : readbacks) {
    const auto* bytes = static_cast<const uint8_t*>(readback.buffer.contents);
    for (size_t y = 0; y < 8; ++y) for (size_t x = 0; x < 64; ++x)
      Check(bytes[y * 256 + x] == readback.expected, "Eviction corrupted queued texture pixels");
  }
  heap->Release(address);
  std::printf("metal_texture_pressure=passed textures=%u unit=%zu queued-readbacks=%zu\n",
              count, unit, readbacks.size());
}
