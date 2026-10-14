#pragma once

#import <Metal/Metal.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rex::ui::metal {

struct UploadSlice {
  id<MTLBuffer> buffer = nil;
  size_t offset = 0;
  void* data = nullptr;
  uint64_t gpu_address = 0;  // Immutable base + slice offset, queried once per slab.
  explicit operator bool() const { return buffer && data; }
};

// CPU-write/GPU-read storage owned by one frame slot. Reset only after completion.
// Growing adds a slab; it never replaces storage referenced by earlier draws.
class UploadArena {
 public:
  static constexpr size_t kDefaultBudget = 8 * 1024 * 1024;
  explicit UploadArena(size_t budget = kDefaultBudget) : budget_(budget) {}
  UploadSlice Allocate(id<MTLDevice> device, size_t size, size_t alignment);
  void Reset();
  size_t reserved_bytes() const { return reserved_bytes_; }
  size_t allocation_count() const { return slabs_.size(); }

 private:
  struct Slab {
    id<MTLBuffer> buffer = nil;
    size_t used = 0;
    size_t capacity = 0;
    std::byte* data = nullptr;
    uint64_t gpu_address = 0;
  };
  std::vector<Slab> slabs_;
  // Keep the current slab hot; wrap only when it cannot satisfy the request.
  // Earlier slabs can still contain useful space for smaller allocations.
  size_t current_slab_ = 0;
  size_t budget_;
  size_t reserved_bytes_ = 0;
};

}  // namespace rex::ui::metal
