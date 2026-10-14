#pragma once
#include <algorithm>
#include <cstddef>
#include <memory_resource>
#include <mutex>
#include <new>
#include <vector>

namespace rex::graphics::gta4_native {

// Pipeline and shader-state snapshots are allocated by the render worker for
// nearly every draw and released on the retirement threads. A synchronized
// pool took its mutex on both sides of every allocation. Here the single
// allocating thread pops from a private free list and takes the lock only to
// adopt everything returned since (or to grow); releases from any thread
// take it once each. Memory is kept for reuse until the resource dies.
class NativeSnapshotResource final : public std::pmr::memory_resource {
 public:
  ~NativeSnapshotResource() override {
    for (void* block : blocks_) ::operator delete(block, std::align_val_t(kAlignment));
  }

 private:
  static constexpr size_t kAlignment = 16;
  static constexpr size_t kChunksPerBlock = 64;
  static constexpr size_t kMaximumClasses = 8;
  struct SizeClass {
    size_t size = 0;
    std::vector<void*> local;     // Allocating thread only.
    std::vector<void*> returned;  // Under mutex_.
  };

  static size_t Rounded(size_t bytes) { return (bytes + kAlignment - 1) & ~(kAlignment - 1); }

  SizeClass* Find(size_t size) {
    for (size_t i = 0; i < class_count_; ++i)
      if (classes_[i].size == size) return &classes_[i];
    return nullptr;
  }

  void* do_allocate(size_t bytes, size_t alignment) override {
    if (alignment > kAlignment || !bytes) return ::operator new(bytes, std::align_val_t(alignment));
    const size_t size = Rounded(bytes);
    SizeClass* size_class = Find(size);
    if (!size_class) {
      std::lock_guard lock(mutex_);
      if (class_count_ == kMaximumClasses) {
        return ::operator new(bytes, std::align_val_t(alignment));
      }
      size_class = &classes_[class_count_];
      size_class->size = size;
      ++class_count_;  // Published to this thread; other threads look up under mutex_.
    }
    if (size_class->local.empty()) {
      std::lock_guard lock(mutex_);
      size_class->local.swap(size_class->returned);
      if (size_class->local.empty()) {
        auto* block = static_cast<std::byte*>(
            ::operator new(size * kChunksPerBlock, std::align_val_t(kAlignment)));
        blocks_.push_back(block);
        size_class->local.reserve(kChunksPerBlock);
        for (size_t i = 0; i < kChunksPerBlock; ++i) size_class->local.push_back(block + i * size);
      }
    }
    void* chunk = size_class->local.back();
    size_class->local.pop_back();
    return chunk;
  }

  void do_deallocate(void* pointer, size_t bytes, size_t alignment) override {
    if (alignment > kAlignment || !bytes) {
      ::operator delete(pointer, std::align_val_t(alignment));
      return;
    }
    const size_t size = Rounded(bytes);
    std::lock_guard lock(mutex_);
    for (size_t i = 0; i < class_count_; ++i) {
      if (classes_[i].size == size) {
        classes_[i].returned.push_back(pointer);
        return;
      }
    }
    // Allocated after the class table filled up.
    ::operator delete(pointer, std::align_val_t(alignment));
  }

  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
    return this == &other;
  }

  std::mutex mutex_;
  SizeClass classes_[kMaximumClasses];
  size_t class_count_ = 0;
  std::vector<void*> blocks_;  // Under mutex_.
};

}  // namespace rex::graphics::gta4_native
