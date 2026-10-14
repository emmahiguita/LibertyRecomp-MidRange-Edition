#include "graphics/gta4_metal/texture_preparation.h"

static std::vector<uint8_t> ReadPreparedImage(const std::shared_ptr<rex::ui::metal::MetalContext>& context,
    id<MTLCommandBuffer> commands, id<MTLTexture> image) {
  const bool compressed = image.pixelFormat == MTLPixelFormatBC1_RGBA ||
      image.pixelFormat == MTLPixelFormatBC2_RGBA || image.pixelFormat == MTLPixelFormatBC3_RGBA;
  std::vector<id<MTLBuffer>> outputs;
  auto blit = [commands blitCommandEncoder];
  for (NSUInteger mip = 0; mip < image.mipmapLevelCount; ++mip) {
    const NSUInteger width = std::max(NSUInteger{1}, image.width >> mip);
    const NSUInteger height = std::max(NSUInteger{1}, image.height >> mip);
    const NSUInteger depth = std::max(NSUInteger{1}, image.depth >> mip);
    const NSUInteger layers = image.textureType == MTLTextureTypeCube ? 6 : image.arrayLength;
    const size_t row = 256, rows = compressed ? (height + 3) / 4 : height, size = row * rows * depth;
    for (NSUInteger layer = 0; layer < layers; ++layer) {
      auto output = [context->device newBufferWithLength:size options:MTLResourceStorageModeShared];
      std::memset(output.contents, 0, size); outputs.push_back(output);
      [blit copyFromTexture:image sourceSlice:layer sourceLevel:mip sourceOrigin:MTLOriginMake(0, 0, 0)
          sourceSize:MTLSizeMake(width, height, depth) toBuffer:output destinationOffset:0
          destinationBytesPerRow:row destinationBytesPerImage:row * rows];
    }
  }
  [blit endEncoding]; [commands commit]; [commands waitUntilCompleted];
  Check(commands.status == MTLCommandBufferStatusCompleted, "Prepared texture GPU readback failed");
  std::vector<uint8_t> result;
  for (auto output : outputs) {
    const auto* bytes = static_cast<const uint8_t*>(output.contents);
    result.insert(result.end(), bytes, bytes + output.length);
  }
  return result;
}

static void ExerciseTexturePreparation(const std::shared_ptr<rex::ui::metal::MetalContext>& context,
    rex::memory::Memory& memory) {
  using namespace rex; using namespace rex::graphics; using namespace rex::graphics::gta4_metal;
  const uint32_t allocation = memory::kMemoryAllocationReserve | memory::kMemoryAllocationCommit;
  const uint32_t protection = memory::kMemoryProtectRead | memory::kMemoryProtectWrite;
  uint32_t backing = 0;
  Check(memory.GetPhysicalHeap()->Alloc(1048576, 4096, allocation, protection, false, &backing), "Preparation fixture allocation failed");
  GuestMemory guest(&memory); ResourceStore store(context, guest); store.BeginFrame();
  auto source = guest.Writable(backing, 1048576, true);
  const xenos::TextureFormat formats[]{xenos::TextureFormat::k_8_8_8_8, xenos::TextureFormat::k_8,
      xenos::TextureFormat::k_DXN, xenos::TextureFormat::k_CTX1, xenos::TextureFormat::k_DXT5A,
      xenos::TextureFormat::k_DXT3A, xenos::TextureFormat::k_DXT1, xenos::TextureFormat::k_DXT2_3,
      xenos::TextureFormat::k_DXT4_5};
  size_t cases = 0;
  for (auto format : formats) for (bool tiled : {false, true})
      for (auto endian : {xenos::Endian::kNone, xenos::Endian::k8in16, xenos::Endian::k8in32, xenos::Endian::k16in32}) {
    for (size_t i = 0; i < source.size(); ++i) source[i] = uint8_t(i * 37 + 11);
    xenos::xe_gpu_texture_fetch_t fetch{};
    fetch.type = xenos::FetchConstantType::kTexture; fetch.format = format;
    fetch.dimension = xenos::DataDimension::k2DOrStacked;
    fetch.base_address = backing >> 12;
    const auto* format_info = FormatInfo::Get(format);
    fetch.pitch = tiled ? 2 : std::max(2u, (256 / format_info->bytes_per_block() * format_info->block_width + 31) / 32);
    fetch.tiled = tiled; fetch.endianness = endian;
    fetch.size_2d.width = fetch.size_2d.height = 15; fetch.swizzle = xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA;
    std::string error;
    auto commands = [context->queue commandBuffer];
    auto reference = store.Texture(1, fetch, commands, [] {}, error); Check(bool(reference), "Inline format=" + std::to_string(unsigned(format)) + " tiled=" + std::to_string(tiled) + " endian=" + std::to_string(unsigned(endian)) + ": " + error);
    const auto expected = ReadPreparedImage(context, commands, reference->image);
    store.PrefetchTexture(2, fetch);
    // Change the guest bytes after snapshotting: a worker must read only its
    // owned snapshot. A later explicit invalidation must observe the new data.
    std::fill(source.begin(), source.end(), uint8_t{0});
    commands = [context->queue commandBuffer];
    auto prepared = store.Texture(2, fetch, commands, [] {}, error); Check(bool(prepared), error);
    Check(ReadPreparedImage(context, commands, prepared->image) == expected,
        "Worker decode differs from inline decode or retained guest pointers");
    store.Invalidate(2); store.PrefetchTexture(2, fetch);
    store.Invalidate(2); // Cancel a queued, running, or completed generation.
    for (size_t i = 0; i < source.size(); ++i) source[i] = uint8_t(i * 37 + 11);
    store.PrefetchTexture(2, fetch);
    commands = [context->queue commandBuffer];
    auto refreshed = store.Texture(2, fetch, commands, [] {}, error); Check(bool(refreshed), error);
    Check(refreshed->generation == prepared->generation, "Identical notified payload was decoded again");
    Check(ReadPreparedImage(context, commands, refreshed->image) == expected, "Cancelled preparation overwrote a newer texture generation");
    store.Release(1); store.Release(2); ++cases;
  }
  for (auto dimension : {xenos::DataDimension::k2DOrStacked, xenos::DataDimension::kCube, xenos::DataDimension::k3D}) {
    xenos::xe_gpu_texture_fetch_t fetch{};
    fetch.type = xenos::FetchConstantType::kTexture; fetch.format = xenos::TextureFormat::k_8_8_8_8;
    fetch.dimension = dimension; fetch.base_address = backing >> 12; fetch.mip_address = (backing + 524288) >> 12;
    fetch.pitch = 2; fetch.tiled = true; fetch.mip_max_level = 1;
    fetch.swizzle = xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA;
    if (dimension == xenos::DataDimension::k3D) {
      fetch.size_3d.width = fetch.size_3d.height = 15; fetch.size_3d.depth = 3;
    } else {
      fetch.size_2d.width = fetch.size_2d.height = 15;
      if (dimension == xenos::DataDimension::k2DOrStacked) { fetch.stacked = true; fetch.size_2d.stack_depth = 3; }
    }
    for (size_t i = 0; i < source.size(); ++i) source[i] = uint8_t(i * 37 + 11);
    std::string error;
    auto commands = [context->queue commandBuffer];
    auto reference = store.Texture(1, fetch, commands, [] {}, error); Check(bool(reference), "Inline dimension=" + std::to_string(unsigned(dimension)) + ": " + error);
    auto expected = ReadPreparedImage(context, commands, reference->image);
    store.PrefetchTexture(2, fetch);
    commands = [context->queue commandBuffer];
    auto prepared = store.Texture(2, fetch, commands, [] {}, error); Check(bool(prepared), error);
    Check(ReadPreparedImage(context, commands, prepared->image) == expected, "Prepared array/cube/volume mip data differs");
    store.Release(1); store.Release(2); ++cases;
    store.PrefetchTexture(3, fetch); store.Release(3);
    store.PrefetchTexture(3, fetch); store.Clear(); // Shutdown/release must not publish cancelled work.
  }
  const auto statistics = store.preparation_statistics();
  Check(statistics.queued && statistics.ready + statistics.waited && statistics.staging_reuses,
      "Preparation fixture failed to exercise workers and staging reuse");
  // Staging storage is unavailable while its command buffer is uncommitted or
  // in flight, and becomes reusable only after GPU completion.
  TexturePreparation uploads;
  auto first_commands = [context->queue commandBuffer];
  auto second_commands = [context->queue commandBuffer];
  auto first = uploads.Staging(context->device, first_commands, 4096);
  auto second = uploads.Staging(context->device, second_commands, 4096);
  Check(first && second && first.buffer != second.buffer, "Upload pool recycled an uncommitted generation");
  [first_commands commit]; [first_commands waitUntilCompleted];
  auto third_commands = [context->queue commandBuffer];
  Check(uploads.Staging(context->device, third_commands, 4096).buffer == first.buffer, "Completed upload storage was not recycled");
  [second_commands commit]; [third_commands commit];
  [second_commands waitUntilCompleted]; [third_commands waitUntilCompleted];
  store.Clear(); memory.GetPhysicalHeap()->Release(backing);
  std::printf("texture_preparation=passed cases=%zu queued=%llu ready=%llu waited=%llu staging_reuses=%llu\n",
      cases, statistics.queued, statistics.ready, statistics.waited, statistics.staging_reuses);
}
