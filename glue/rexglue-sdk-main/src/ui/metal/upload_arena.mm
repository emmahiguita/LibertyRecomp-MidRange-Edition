#include "upload_arena.h"

#include <algorithm>
#include <limits>

namespace rex::ui::metal {
namespace {
bool Align(size_t value, size_t alignment, size_t& result) {
  if (!alignment || (alignment & (alignment - 1)) ||
      value > std::numeric_limits<size_t>::max() - (alignment - 1)) return false;
  result = (value + alignment - 1) & ~(alignment - 1);
  return true;
}
}  // namespace

UploadSlice UploadArena::Allocate(id<MTLDevice> device, size_t size, size_t alignment) {
  size_t padded_size;
  if (!device || !size || !Align(size, alignment, padded_size) || padded_size > budget_) return {};
  size_t index = current_slab_;
  for (size_t visited = 0; visited < slabs_.size(); ++visited) {
    auto& slab = slabs_[index];
    size_t start;
    if (Align(slab.used, alignment, start) && start <= slab.capacity &&
        size <= slab.capacity - start) {
      slab.used = start + size;
      current_slab_ = index;
      return {slab.buffer, start, slab.data + start, slab.gpu_address + start};
    }
    if (++index == slabs_.size()) index = 0;
  }
  const size_t remaining = budget_ - reserved_bytes_;
  if (padded_size > remaining) return {};
  constexpr size_t kSlabSize = 64 * 1024;
  const size_t capacity = std::min(remaining, std::max(kSlabSize, padded_size));
  // Constant reuse compares these bytes on the CPU. Write-combined storage
  // makes those reads exceptionally expensive; use ordinary cached memory.
  id<MTLBuffer> buffer = [device newBufferWithLength:capacity
      options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeDefaultCache |
              MTLResourceHazardTrackingModeTracked];
  if (!buffer) return {};
  auto* data = static_cast<std::byte*>(buffer.contents);
  if (!data) return {};
  buffer.label = @"Liberty Metal frame upload";
  // Metal buffer storage is immutable for its lifetime. Cache its CPU mapping
  // and requested capacity instead of messaging the driver for every slice.
  const uint64_t gpu_address = buffer.gpuAddress;
  slabs_.push_back({buffer, size, capacity, data, gpu_address});
  current_slab_ = slabs_.size() - 1;
  reserved_bytes_ += capacity;
  return {buffer, 0, data, gpu_address};
}

void UploadArena::Reset() {
  for (auto& slab : slabs_) slab.used = 0;
  current_slab_ = 0;
}

}  // namespace rex::ui::metal
