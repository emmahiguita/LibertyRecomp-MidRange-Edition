#include "resource_state.h"
#include "../gta4_native/core/font_atlas.h"
#include <rex/graphics/pipeline/texture/conversion.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/logging.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <rex/diagnostics/policy.h>
#include <cstring>

namespace rex::graphics::gta4_metal {
void ResourceStore::RegisterFont(uint32_t texture, uint32_t font_id) {
  // A later ordinary bind does not erase an owner identity established by the
  // title font hook. Destruction and changed owner identity retire it.
  if (!texture || !font_id || font_id > 3) return;
  if (rex::diagnostics::IsEnabled(rex::diagnostics::Category::kNativeTrace)) {
    static std::atomic<uint32_t> traces{0};
    const uint32_t trace = traces.fetch_add(1, std::memory_order_relaxed);
    if (trace < 12) REXLOG_INFO("gta4-metal-fonts: register font={} texture={:08X}", font_id, texture);
  }
  auto [entry, inserted] = state_->font_ids.try_emplace(texture, font_id);
  if (inserted || entry->second != font_id) {
    state_->InvalidateBindings();
    entry->second = font_id;
    state_->dirty.insert(texture);
  }
}

std::shared_ptr<TextureResource> ResourceStore::State::FontTexture(uint32_t handle,
    uint32_t font_id, const xenos::xe_gpu_texture_fetch_t& fetch,
    id<MTLCommandBuffer> commands, const std::function<void()>& end_render, std::string& error) {
  using namespace gta4_native::core;
  TextureInfo info{};
  if (!font_id || font_id > 3 || !TextureInfo::Prepare(fetch, &info) ||
      info.dimension != xenos::DataDimension::k2DOrStacked || info.is_stacked ||
      info.format != xenos::TextureFormat::k_DXT4_5 || info.width != 511 || info.height != 511 ||
      info.mip_min_level != 0 || info.mip_max_level != 0) return {};
  const auto layout = texture_util::GetGuestTextureLayout(info.dimension, info.pitch >> 5,
      info.width + 1, info.height + 1, info.depth + 1, info.is_tiled, info.format,
      info.has_packed_mips, info.memory.base_address != 0, info.mip_max_level);
  uint32_t packed_x = 0, packed_y = 0;
  const auto address = info.GetMipLocation(0, &packed_x, &packed_y, true);
  const auto source = memory.Read(address, layout.base.level_data_extent_bytes, true);
  if (source.empty()) { error = "Font atlas guest payload is not readable"; return {}; }
  const auto extent = info.GetMipExtent(0, true);
  std::vector<uint8_t> linear(kFontIdentityBytes);
  constexpr uint32_t blocks = 128, block_bytes = 16;
  for (uint32_t y = 0; y < blocks; ++y) for (uint32_t x = 0; x < blocks; ++x) {
    const int64_t offset = info.is_tiled
        ? texture_util::GetTiledOffset2D(x + packed_x, y + packed_y, extent.block_pitch_h, 4)
        : int64_t((uint64_t(y + packed_y) * extent.block_pitch_h + x + packed_x) * block_bytes);
    if (offset < 0 || uint64_t(offset) > source.size() || block_bytes > source.size() - size_t(offset)) {
      error = "Font atlas block exceeds the validated guest layout"; return {};
    }
    texture_conversion::CopySwapBlock(info.endianness, linear.data() + (size_t(y) * blocks + x) * block_bytes,
                                     source.data() + offset, block_bytes);
  }
  const uint64_t identity = StockFontIdentity(linear);
  const auto set = SelectVectorFontSet(font_id - 1, identity);
  const auto* atlas = FindVectorFontAtlas(set, font_id - 1);
  if (!atlas) return {}; // Keep the game font when a replacement asset is unavailable.
  const auto mips = FontCoverageMips(atlas->alpha, kFontAtlasExtent, kFontMipLevels);
  if (mips.size() != kFontMipLevels) { error = "Invalid font coverage mip chain"; return {}; }
  auto descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
      width:kFontAtlasExtent height:kFontAtlasExtent mipmapped:NO];
  descriptor.mipmapLevelCount = kFontMipLevels;
  descriptor.storageMode = MTLStorageModePrivate;
  descriptor.hazardTrackingMode = MTLHazardTrackingModeTracked;
  descriptor.usage = MTLTextureUsageShaderRead;
  if (!AdmitTexture([context->device heapTextureSizeAndAlignWithDescriptor:descriptor].size,
                    handle, false, error)) return {};
  auto image = [context->device newTextureWithDescriptor:descriptor];
  if (!image) { error = "Font texture allocation failed"; return {}; }
  auto view = [image newTextureViewWithPixelFormat:image.pixelFormat textureType:MTLTextureType2D
      levels:NSMakeRange(0, kFontMipLevels) slices:NSMakeRange(0, 1)
      swizzle:MTLTextureSwizzleChannels{MTLTextureSwizzleOne, MTLTextureSwizzleOne,
                                       MTLTextureSwizzleOne, MTLTextureSwizzleRed}];
  if (!view) { error = "Font coverage texture view creation failed"; return {}; }
  std::array<size_t, kFontMipLevels> offsets{}, pitches{};
  size_t total = 0;
  for (size_t i = 0; i < mips.size(); ++i) {
    const size_t size = kFontAtlasExtent >> i;
    offsets[i] = total; pitches[i] = (size + 255) & ~size_t(255);
    total += pitches[i] * size;
  }
  auto staging=preparation.Staging(context->device,commands,total);
  if (!staging) { error = "Font upload allocation failed"; return {}; }
  for (size_t i = 0; i < mips.size(); ++i) {
    const size_t size = kFontAtlasExtent >> i;
    for (size_t y = 0; y < size; ++y)
      std::memcpy(static_cast<uint8_t*>(staging.data) + offsets[i] + y * pitches[i],
                  mips[i].data() + y * size, size);
  }
  end_render();
  auto blit = [commands blitCommandEncoder];
  if (!blit) { error = "Font upload encoder unavailable"; return {}; }
  blit.label = @"Liberty high-resolution font upload";
  for (size_t i = 0; i < mips.size(); ++i) {
    const NSUInteger size = kFontAtlasExtent >> i;
    [blit copyFromBuffer:staging.buffer sourceOffset:staging.offset+offsets[i] sourceBytesPerRow:pitches[i]
        sourceBytesPerImage:pitches[i] * size sourceSize:MTLSizeMake(size, size, 1)
        toTexture:image destinationSlice:0 destinationLevel:i destinationOrigin:MTLOriginMake(0, 0, 0)];
  }
  [blit endEncoding];
  image.label = [NSString stringWithFormat:@"Liberty font %u %s", font_id, VectorFontSetName(set)];
  auto resource = std::make_shared<TextureResource>();
  resource->image = image; resource->font_view = view;
  resource->fetch = fetch; resource->info = info; // Guest dimensions/layout remain authoritative.
  resource->font_id = font_id; resource->font_replacement = true;
  resource->initialized = resource->storage_initialized = true;
  resource->generation = next_generation++; resource->use_serial = ++serial;
  REXLOG_INFO("gta4-metal-fonts: font={} handle={:08X} set={} identity={:016X} size={} mips={}",
      font_id, handle, VectorFontSetName(set), identity, image.width, image.mipmapLevelCount);
  return resource;
}
}  // namespace rex::graphics::gta4_metal
