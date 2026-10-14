#include "vertex_conversion_plan.h"
#include <xxhash.h>
#include "resource_state.h"
#include "guest_state.h"
#include "guest_access_cache.h"
#include "../gta4_native/native_frame_scheduling.h"
#include <rex/memory.h>
#include <algorithm>
#include <cstring>
#include <limits>

namespace rex::graphics::gta4_metal {
namespace {
memory::BaseHeap* ReadableHeap(memory::Memory* memory, uint32_t address, size_t size,
                               bool physical, bool writable) {
  const uint64_t limit = physical ? uint64_t{0x20000000} : uint64_t{0x100000000};
  if (!memory || !size || address >= limit || size > limit - address) return nullptr;
  auto* heap = physical ? memory->GetPhysicalHeap() : memory->LookupHeap(address);
  if (!heap) return nullptr;
  const uint32_t end = uint32_t(uint64_t(address) + size - 1);
  if (address < heap->heap_base() || uint64_t(end) - heap->heap_base() >= heap->heap_size()) return nullptr;
  const uint32_t first_page = (address - heap->heap_base()) / heap->page_size();
  const uint32_t last_page = (end - heap->heap_base()) / heap->page_size();
  const uint64_t epoch = heap->access_epoch();
  thread_local GuestAccessCache permissions;
  memory::PageAccess access;
  if (!permissions.Find(heap, epoch, first_page, last_page, access)) {
    access = heap->QueryRangeAccess(address, end);
    permissions.Remember(heap, epoch, heap->access_epoch(), first_page, last_page, access);
  }
  const auto required = writable ? memory::PageAccess::kReadWrite : memory::PageAccess::kReadOnly;
  return (uint32_t(access)&uint32_t(required)) == uint32_t(required) ? heap : nullptr;
}
}

std::span<const uint8_t> GuestMemory::Read(uint32_t address, size_t size, bool physical) const {
  if (!ReadableHeap(memory_,address,size,physical,false)) return {};
  return {physical ? memory_->TranslatePhysical<const uint8_t*>(address)
                   : memory_->TranslateVirtual<const uint8_t*>(address),size};
}
std::span<uint8_t> GuestMemory::Writable(uint32_t address, size_t size, bool physical) const {
  if (!ReadableHeap(memory_, address, size, physical, true)) return {};
  return {physical ? memory_->TranslatePhysical<uint8_t*>(address)
                   : memory_->TranslateVirtual<uint8_t*>(address), size};
}
bool GuestMemory::Write(uint32_t address, std::span<const uint8_t> bytes, bool physical) const {
  if (!ReadableHeap(memory_,address,bytes.size(),physical,true)) return false;
  auto* target = physical ? memory_->TranslatePhysical<uint8_t*>(address)
                          : memory_->TranslateVirtual<uint8_t*>(address);
  std::memcpy(target,bytes.data(),bytes.size()); return true;
}
gta4_native::SurfaceDescriptor GuestMemory::Surface(uint32_t handle) const {
  return DecodeSurface(handle,Read(handle,44));
}

ResourceStore::State::Buffer* ResourceStore::State::CaptureBuffer(uint32_t handle, std::string& error) {
  using namespace gta4_native;
  const auto header = memory.Read(handle,32);
  if (header.empty()) { error="Unmapped guest buffer header"; return nullptr; }
  const uint32_t flags=GuestWord(header,0);
  const auto metadata=DecodeNativeBufferMetadata(flags,GuestWord(header,24),GuestWord(header,28));
  if (!metadata || !metadata->HasValidPayload(64u*1024u*1024u)) {
    error="Invalid guest buffer metadata"; return nullptr;
  }
  auto found=buffers.find(handle);
  const bool matches=found!=buffers.end() && found->second.address==metadata->guest_address &&
      found->second.size==metadata->guest_size && (found->second.flags&0x8000000Fu)==(flags&0x8000000Fu);
  if (matches && !dirty.contains(handle) && !metadata->guest_locked) {
    auto& cached=found->second;
    const auto range=GetNativeBufferShadowValidationRange(cached.payload.size(),cached.validation_offset);
    auto slice=memory.Read(metadata->guest_address+uint32_t(range.offset),range.length);
    if (slice.size()==range.length && !std::memcmp(slice.data(),cached.payload.data()+range.offset,range.length)) {
      cached.validation_offset=range.next_offset;
      TouchBuffer(cached);
      return &cached;
    }
  }
  const auto bytes=memory.Read(metadata->guest_address,metadata->guest_size);
  if (bytes.empty()) { error="Unmapped guest buffer payload"; return nullptr; }
  if (matches && found->second.payload.size()==bytes.size() &&
      !std::memcmp(bytes.data(),found->second.payload.data(),bytes.size())) {
    dirty.erase(handle); TouchBuffer(found->second); return &found->second;
  }
  // Captures can be recreated from guest memory. A full cache must evict an
  // old capture, not drop a valid game draw. Encoded Metal buffers and buffers
  // gathered for the current draw retain their own immutable generations.
  if (bytes.size() > buffer_capture_budget) {
    error = "Guest buffer exceeds the single-capture budget"; return nullptr;
  }
  Buffer value;
  value.generation=next_generation++;
  value.flags=flags; value.address=metadata->guest_address; value.size=metadata->guest_size;
  value.payload.assign(bytes.begin(),bytes.end());
  EraseBuffer(handle);
  constexpr size_t kMaximumBufferEntries = 65536;
  while (captured_buffer_bytes > buffer_capture_budget - bytes.size() ||
         buffers.size() >= kMaximumBufferEntries) {
    if (buffer_lru.empty()) { error = "Buffer cache accounting invariant failed"; return nullptr; }
    const uint32_t victim = buffer_lru.front();
    EraseBuffer(victim);
    dirty.erase(victim);
    ++buffer_evictions;
  }
  buffer_lru.push_back(handle);
  value.recency = std::prev(buffer_lru.end());
  auto inserted=buffers.emplace(handle,std::move(value));
  captured_buffer_bytes += inserted.first->second.payload.size();
  dirty.erase(handle); return &inserted.first->second;
}

void ResourceStore::State::EraseVertexConversion(Buffer& buffer,
    std::map<ConversionKey, Conversion>::iterator entry) {
  const auto& value = entry->second;
  vertex_conversion_lru.erase(value.recency);
  vertex_conversion_bytes -= value.allocated_bytes;
  vertex_conversion_budget_bytes -= value.charged_bytes;
  resident_buffer_bytes -= value.allocated_bytes;
  // Only cache ownership is released. Encoded commands and callers retain the
  // immutable buffer; its storage is never overwritten for another generation.
  buffer.vertices.erase(entry);
}

void ResourceStore::State::EraseBufferConversions(Buffer& buffer) {
  while (!buffer.vertices.empty()) EraseVertexConversion(buffer, buffer.vertices.begin());
}

void ResourceStore::State::TrimVertexConversions(size_t incoming_bytes) {
  while (vertex_conversion_budget_bytes > vertex_conversion_budget - incoming_bytes) {
    auto* victim = vertex_conversion_lru.front();
    auto& owner = *victim->owner;
    const auto entry = owner.vertices.find({victim->declaration, victim->shader,
        victim->stream, victim->offset, victim->stride});
    EraseVertexConversion(owner, entry);
    ++vertex_conversion_evictions;
  }
}

uint64_t ResourceStore::CapturedBufferGeneration(uint32_t handle) const {
  const auto found=state_->buffers.find(handle);
  return found==state_->buffers.end()?0:found->second.generation;
}

id<MTLBuffer> ResourceStore::VertexBuffer(uint32_t handle,const VertexDeclaration& declaration,
    const ShaderMetadata& shader,uint32_t stream,uint32_t offset,uint32_t stride,std::string& error) {
  @autoreleasepool {
    error.clear();
    if (!stride || stream>=gta4_native::kVertexStreamCount) { error="Invalid vertex stream"; return nil; }
    auto* source=state_->CaptureBuffer(handle,error); if (!source) return nil;
    if ((source->flags&0xFu)!=1 || offset>=source->payload.size()) { error="Vertex buffer range/type mismatch"; return nil; }
    struct Input {uint32_t location; MetalVertexScalar numeric_type;};
    std::array<Input, kMetalArchiveMaximumAttributes> attributes{};
    if (shader.attribute_count > attributes.size()) {error="Invalid vertex metadata count"; return nil;}
    for (uint32_t i = 0; i < shader.attribute_count; ++i)
      attributes[i] = {shader.attributes[i].semantic_location, shader.attributes[i].scalar_type};
    struct Inputs {std::span<const Input> vertex_inputs;} inputs{{attributes.data(), shader.attribute_count}};
    if(declaration.elements.size()>gta4_native::kMaximumVertexElementCount){error="Vertex declaration exceeds its bound";return nil;}
    const auto plan=VertexCorrections(declaration,shader,stream,stride);
    // Shader/declaration identities do not alter bytes. Only effective ordered
    // correction operations do. Hash collisions always receive an exact check.
    const auto steps=plan.values();
    const uint64_t key_declaration=steps.empty()?0:XXH3_64bits(steps.data(),steps.size_bytes());
    uint64_t key_shader=0;const uint32_t key_stream=0;
    const uint32_t key_offset=steps.empty()?0:offset,key_stride=steps.empty()?0:stride;
    State::ConversionKey key;
    for(;;++key_shader){
      key={key_declaration,key_shader,key_stream,key_offset,key_stride};
      const auto entry=source->vertices.find(key);
      if(entry==source->vertices.end())break;
      auto& converted=entry->second;
      if(converted.corrections.size()!=steps.size()||!std::equal(steps.begin(),steps.end(),converted.corrections.begin()))continue;
      ++state_->vertex_conversion_hits;
      state_->vertex_conversion_lru.splice(state_->vertex_conversion_lru.end(),state_->vertex_conversion_lru,converted.recency);
      return converted.buffer;
    }
    ++state_->vertex_conversion_misses;
    auto buffer=[state_->context->device newBufferWithLength:source->payload.size()
        options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeDefaultCache];
    if (!buffer) { error="Metal vertex allocation failed"; return nil; }
    auto* destination = static_cast<uint8_t*>(buffer.contents);
    if (!destination) { error="Metal vertex mapping failed"; return nil; }
    gta4_native::core::ConvertGuestVertexPayload(destination,source->payload.data(),
        source->payload.size(),declaration,inputs,stream,offset,stride);
    buffer.label=@"Liberty immutable vertex generation";
    const size_t allocated_bytes = buffer.allocatedSize;
    const size_t metadata_charge=State::kVertexConversionEntryCharge+steps.size_bytes();
    // An individually oversized driver allocation remains usable by its draw,
    // but is not admitted into the bounded retained conversion cache.
    if (state_->vertex_conversion_budget < metadata_charge ||
        allocated_bytes > state_->vertex_conversion_budget - metadata_charge) return buffer;
    state_->TrimVertexConversions(allocated_bytes + metadata_charge);
    auto entry = source->vertices.emplace(key, State::Conversion{
        key_declaration,key_shader,key_stream,key_offset,key_stride,buffer,allocated_bytes,source,{},std::vector<uint64_t>(steps.begin(),steps.end()),allocated_bytes+metadata_charge}).first;
    auto& converted = entry->second;
    state_->vertex_conversion_lru.push_back(&converted);
    converted.recency = std::prev(state_->vertex_conversion_lru.end());
    state_->vertex_conversion_bytes += allocated_bytes;
    state_->vertex_conversion_budget_bytes += allocated_bytes + metadata_charge;
    state_->resident_buffer_bytes += allocated_bytes;
    return buffer;
  }
}

id<MTLBuffer> ResourceStore::IndexBuffer(uint32_t handle,bool& index32,std::string& error) {
  @autoreleasepool {
    error.clear(); index32=false;
    auto* source=state_->CaptureBuffer(handle,error); if (!source) return nil;
    if ((source->flags&0xFu)!=2) {error="Index buffer has wrong resource type"; return nil;}
    index32=(source->flags&0x80000000u)!=0;
    const size_t element_size=index32 ? sizeof(uint32_t) : sizeof(uint16_t);
    if (source->payload.size()%element_size) {error="Misaligned index payload"; return nil;}
    if (source->indices) return source->indices;
    auto buffer=[state_->context->device newBufferWithLength:source->payload.size()
        options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeDefaultCache];
    if (!buffer) {error="Metal index allocation failed"; return nil;}
    auto* destination = static_cast<uint8_t*>(buffer.contents);
    if (!destination) {error="Metal index mapping failed"; return nil;}
    for (size_t offset=0;offset<source->payload.size();offset+=element_size) {
      if (index32) {
        uint32_t value; std::memcpy(&value,source->payload.data()+offset,sizeof(value));
        value=__builtin_bswap32(value)&xenos::kVertexIndexMask;
        std::memcpy(destination+offset,&value,sizeof(value));
      } else {
        uint16_t value; std::memcpy(&value,source->payload.data()+offset,sizeof(value));
        value=__builtin_bswap16(value);
        std::memcpy(destination+offset,&value,sizeof(value));
      }
    }
    buffer.label=@"Liberty immutable index generation"; source->indices=buffer;
    state_->resident_buffer_bytes += buffer.allocatedSize; return buffer;
  }
}

size_t ResourceStore::buffer_bytes() const {
  return state_->captured_buffer_bytes + state_->resident_buffer_bytes;
}
ResourceStore::BufferCacheStatistics ResourceStore::buffer_cache_statistics() const {
  return {state_->captured_buffer_bytes, state_->buffers.size(), state_->buffer_evictions};
}

}  // namespace rex::graphics::gta4_metal
