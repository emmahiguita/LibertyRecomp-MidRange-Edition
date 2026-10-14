#pragma once
#import <Metal/Metal.h>
#include <array>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include <rex/graphics/gta4_native/title_commands.h>
#include <rex/graphics/pipeline/texture/info.h>
#include "../gta4_native/core/draw_state.h"
#include "../gta4_native/native_virtual_resource_registry.h"
#include "shaders.h"
#include "pending_clear.h"
#include "resolve_reuse.h"

namespace rex::memory { class Memory; }
namespace rex::ui::metal { struct MetalContext; }
namespace rex::graphics::gta4_metal {

class GuestMemory {
 public:
  explicit GuestMemory(memory::Memory* memory) : memory_(memory) {}
  std::span<const uint8_t> Read(uint32_t address, size_t size, bool physical = false) const;
  bool Write(uint32_t address, std::span<const uint8_t> bytes, bool physical = false) const;
  std::span<uint8_t> Writable(uint32_t address, size_t size, bool physical = false) const;
  gta4_native::SurfaceDescriptor Surface(uint32_t handle) const;
 private:
  memory::Memory* memory_;
};

struct TextureResource {
  id<MTLTexture> image = nil;
  xenos::xe_gpu_texture_fetch_t fetch{};
  TextureInfo info{};
  uint64_t generation = 0;
  uint64_t use_serial = 0;
  // Valid only while this generation belongs to ResourceStore's named cache.
  std::list<uint32_t>::iterator recency;
  bool gpu_produced = false;
  uint32_t font_id = 0;
  bool font_replacement = false;
  id<MTLTexture> font_view = nil;
  bool initialized = false;
  // Complete deterministic storage initialization is distinct from title writes.
  bool storage_initialized = false;
  std::vector<uint64_t> initialized_subresources;
  bool packed_alias = false;
  uint64_t content_serial = 0;
  uint64_t packed_source_generation = 0, packed_source_serial = 0;
  uint32_t packed_swizzle = 0;
  // A serial exists only after the requested subresource was actually produced.
  std::unordered_map<uint64_t, uint64_t> subresource_writes;
  ResolveReuseRecord last_color_resolve;
};
struct SurfaceResource {
  PendingClear pending_clear{};
  id<MTLTexture> image = nil;
  gta4_native::SurfaceDescriptor descriptor{};
  uint64_t generation = 0;
  bool depth = false;
  bool initialized = false;
  uint32_t content_mask = 0;
  uint64_t content_serial = 0;
};
struct VertexStream {
  uint32_t buffer = 0, offset = 0, stride = 0;
};
struct VertexDeclaration {
  uint64_t identity = 0;
  std::vector<gta4_native::VertexElement> elements;
};

// One encoder owner calls this store. Device-created tracked allocations only.
// API command buffers retain used objects; guest Release removes only the name.
struct MaterialBinding;
class ResourceStore {
 public:
  // A smaller budget is useful for deterministic pressure tests; production
  // retains the existing limit. Eviction drops cache ownership, not GPU work.
  static constexpr size_t kDefaultBufferCaptureBudget = 256u * 1024u * 1024u;
  ResourceStore(std::shared_ptr<ui::metal::MetalContext> context, GuestMemory memory,
                size_t buffer_capture_budget = kDefaultBufferCaptureBudget,
                size_t texture_budget = SIZE_MAX, size_t vertex_conversion_budget = SIZE_MAX);
  ~ResourceStore();
  void PrefetchTexture(uint32_t handle, const xenos::xe_gpu_texture_fetch_t& fetch);
  struct PreparationStatistics {
    uint64_t queued, ready, waited, inline_decodes, staging_allocations, staging_reuses;
    size_t pending_bytes, staging_bytes;
  };
  PreparationStatistics preparation_statistics() const;
  std::shared_ptr<SurfaceResource> Surface(const gta4_native::SurfaceDescriptor& descriptor,
                                          bool depth, std::string& error, uint32_t sample_override = 0);
  std::shared_ptr<TextureResource> Texture(uint32_t handle, const xenos::xe_gpu_texture_fetch_t& fetch,
      id<MTLCommandBuffer> commands, const std::function<void()>& end_render, std::string& error);
  std::shared_ptr<TextureResource> ResolveTarget(const gta4_native::ResolveCommand& command,
                                                std::string& error);
  // Define only unwritten storage; never relabel initialization as a title resolve.
  bool InitializeTextureStorage(const std::shared_ptr<TextureResource>& resource,
      id<MTLCommandBuffer> commands, const std::function<void()>& end_render,
      std::string& error);
  id<MTLTexture> View(const std::shared_ptr<TextureResource>& resource,
                      const xenos::xe_gpu_texture_fetch_t& fetch, std::string& error);
  id<MTLSamplerState> Sampler(const xenos::xe_gpu_texture_fetch_t& fetch, std::string& error,
                               const TextureResource* texture = nullptr);
  void BeginFrame();
  const MaterialBinding* FindMaterialBinding(uint32_t handle,const xenos::xe_gpu_texture_fetch_t& fetch);
  const MaterialBinding* RememberMaterialBinding(uint32_t handle,const xenos::xe_gpu_texture_fetch_t& fetch,
      const std::shared_ptr<TextureResource>& resource,id<MTLTexture> image,id<MTLSamplerState> sampler);
  struct MaterialStatistics {uint64_t hits,misses;};
  MaterialStatistics material_statistics() const;
  void RegisterFont(uint32_t texture, uint32_t font_id);
  id<MTLBuffer> VertexBuffer(uint32_t handle, const VertexDeclaration& declaration,
      const ShaderMetadata& shader, uint32_t stream, uint32_t offset, uint32_t stride,
      std::string& error);
  id<MTLBuffer> IndexBuffer(uint32_t handle, bool& index32, std::string& error);
  uint64_t CapturedBufferGeneration(uint32_t handle) const;
  bool ExchangeResolveImages(const std::shared_ptr<SurfaceResource>& source,const std::shared_ptr<TextureResource>& destination);
  std::shared_ptr<TextureResource> FindTexture(uint32_t handle) const;
  std::shared_ptr<SurfaceResource> FindSurface(uint32_t handle) const;
  // Latest produced color view in the exact title sample space.
  // Explicit depth handoffs and reflection ownership remain separate.
  std::shared_ptr<SurfaceResource> FindColorResolveSource(
      const gta4_native::SurfaceDescriptor& descriptor) const;
  bool IsReflection(uint32_t handle) const;
  bool IsWaterReflection(uint32_t handle) const;
  std::string DescribeSurfaceForDiagnostics(const gta4_native::SurfaceDescriptor&) const;
  const gta4_native::NativeVirtualResourceRecord* Virtual(uint32_t handle) const;
  void PublishWrite(uint32_t handle);
  bool RegisterVirtual(const gta4_native::RegisterVirtualResourceCommand& command);
  void RegisterReflection(const gta4_native::RegisterReflectionTargetCommand& command);
  void Invalidate(uint32_t handle);
  void Release(uint32_t handle);
  void Clear();
  size_t texture_bytes() const;
  size_t buffer_bytes() const;
  struct BufferCacheStatistics { size_t captured_bytes; size_t entries; uint64_t evictions; };
  BufferCacheStatistics buffer_cache_statistics() const;
  struct VertexConversionStatistics {
    size_t bytes, charged_bytes, budget, entries;
    uint64_t hits, misses, evictions;
  };
  VertexConversionStatistics vertex_conversion_statistics() const;
  struct TextureCacheStatistics {
    size_t bytes, budget, entries;
    uint64_t evictions, rejections;
  };
  TextureCacheStatistics texture_cache_statistics() const;
  static MTLPixelFormat TextureFormat(xenos::TextureFormat format);
  static MTLPixelFormat SurfaceFormat(uint32_t format, bool depth);

 private:
  struct State;
  std::unique_ptr<State> state_;
};
}  // namespace rex::graphics::gta4_metal
