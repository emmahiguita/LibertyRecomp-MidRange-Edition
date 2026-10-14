#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>

namespace rex::graphics::gta4_native {

// Copy of one title command. Draw, state and present commands fit inline, so
// the producer no longer pays a heap allocation (and its zero fill) for every
// submitted command; larger registrations fall back to the heap. resize()
// leaves the contents unspecified: callers copy the command in right after.
class NativeCommandBytes {
 public:
  static constexpr size_t kInlineCapacity = 192;

  NativeCommandBytes() {}
  NativeCommandBytes(const NativeCommandBytes& other) { *this = other; }
  NativeCommandBytes& operator=(const NativeCommandBytes& other) {
    if (this != &other) {
      resize(other.size_);
      if (size_) std::memcpy(data(), other.data(), size_);
    }
    return *this;
  }

  void resize(size_t size) {
    if (size > kInlineCapacity) {
      if (size > heap_capacity_) {
        heap_.reset(new uint8_t[size]);
        heap_capacity_ = size;
      }
    } else {
      heap_.reset();
      heap_capacity_ = 0;
    }
    size_ = size;
  }
  uint8_t* data() { return heap_ ? heap_.get() : inline_; }
  const uint8_t* data() const { return heap_ ? heap_.get() : inline_; }
  size_t size() const { return size_; }
  bool empty() const { return !size_; }
  size_t capacity() const { return heap_ ? heap_capacity_ : 0; }
  operator std::span<const uint8_t>() const { return {data(), size_}; }

 private:
  alignas(16) uint8_t inline_[kInlineCapacity];
  std::unique_ptr<uint8_t[]> heap_;
  size_t heap_capacity_ = 0;
  size_t size_ = 0;
};

}  // namespace rex::graphics::gta4_native
