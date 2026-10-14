#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <span>
#include <vector>

namespace rex::graphics::gta4_native {
struct NativeTriangleFanKey {
  uint32_t first = 0, count = 0, restart_index = 0;
  bool index32 = false, restart = false;
  bool operator==(const NativeTriangleFanKey&) const = default;
};
// Explicit emission avoids count underflow and never advertises an unwritten
// index. Cyclic rotation retains winding and Vulkan's first provoking vertex
// for each fan triangle. Restart tests happen before base-vertex addition.
template <typename Read, typename Restart, typename Emit>
inline void VisitNativeTriangleFan(uint32_t count, Read read, Restart restart, Emit emit) {
  uint32_t anchor = 0, previous = 0, filled = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t value = read(i);
    if (restart(value)) { filled = 0; continue; }
    if (filled == 0) { anchor = value; filled = 1; }
    else if (filled == 1) { previous = value; filled = 2; }
    else { emit(previous, value, anchor); previous = value; }
  }
}
inline std::shared_ptr<const std::vector<uint8_t>> BuildNativeTriangleFan(
    std::span<const uint8_t> host_indices, const NativeTriangleFanKey& key,
    uint32_t guest_index_mask) {
  const size_t element = key.index32 ? sizeof(uint32_t) : sizeof(uint16_t);
  const uint64_t end = uint64_t(key.first) + key.count;
  if (end > host_indices.size() / element ||
      (key.count > 2 && uint64_t(key.count - 2) * 3 > UINT32_MAX)) return {};
  auto output = std::make_shared<std::vector<uint8_t>>();
  const auto read = [&](uint32_t i) {
    const auto* p = host_indices.data() + (size_t(key.first) + i) * element;
    if (key.index32) { uint32_t v; std::memcpy(&v,p,sizeof(v)); return v; }
    uint16_t v; std::memcpy(&v,p,sizeof(v)); return uint32_t(v);
  };
  const auto restart = [&](uint32_t v) {
    return key.restart && ((key.index32 ? v & guest_index_mask : v) == key.restart_index);
  };
  uint32_t emitted = 0;
  VisitNativeTriangleFan(key.count, read, restart,
      [&](uint32_t, uint32_t, uint32_t) { emitted += 3; });
  if (uint64_t(emitted) * element > output->max_size()) return {};
  output->resize(size_t(emitted) * element);
  size_t cursor = 0;
  VisitNativeTriangleFan(key.count, read, restart, [&](uint32_t a, uint32_t b, uint32_t c) {
    for (uint32_t v : {a,b,c}) {
      if (key.index32) std::memcpy(output->data()+cursor,&v,sizeof(v));
      else { const uint16_t small = uint16_t(v); std::memcpy(output->data()+cursor,&small,sizeof(small)); }
      cursor += element;
    }
  });
  return output;
}
inline bool WriteNativeSequentialFan(std::span<uint32_t> output, uint32_t count,
                                     uint32_t first = 0) {
  const uint64_t needed = count > 2 ? uint64_t(count - 2) * 3 : 0;
  if (needed > output.size() || uint64_t(first) + count > uint64_t(UINT32_MAX) + 1) return false;
  size_t cursor = 0;
  VisitNativeTriangleFan(count, [&](uint32_t i) {return first+i;},
      [](uint32_t) {return false;}, [&](uint32_t a,uint32_t b,uint32_t c) {
        output[cursor++]=a; output[cursor++]=b; output[cursor++]=c;
      });
  return true;
}
// Owned by one immutable source generation and used only by the render worker.
// Bound CPU variants; returned shared ownership keeps an evicted entry valid
// through the current upload and diagnostics. GPU buffers use the source's
// existing submission-aware persistent-buffer retirement mechanism.
class NativeTriangleFanCache {
 public:
  std::shared_ptr<const std::vector<uint8_t>> Get(std::span<const uint8_t> source,
      const NativeTriangleFanKey& key, uint32_t mask) {
    for (const auto& e : entries_) if (e.key == key && e.mask == mask) return e.bytes;
    auto bytes = BuildNativeTriangleFan(source,key,mask);
    if (!bytes) return {};
    if (entries_.size() == 64) entries_.pop_front();
    entries_.push_back({key,mask,bytes});
    return bytes;
  }
  size_t size() const { return entries_.size(); }
 private:
  struct Entry { NativeTriangleFanKey key; uint32_t mask; std::shared_ptr<const std::vector<uint8_t>> bytes; };
  std::deque<Entry> entries_;
};
}  // namespace rex::graphics::gta4_native
