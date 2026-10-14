#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace rex::graphics::gta4_native {

// Include implicit host-pass inputs as well as title sampler bindings. A queued
// composite owns its half-scene generation even when the title never binds it.
template <typename Command, typename Visitor>
void VisitNativeCommandTextureGenerations(const Command& command, Visitor&& visit) {
  const auto texture = [&](const auto& resource) {
    if (!resource) return;
    visit(resource->generation);
    if (resource->packed_depth_source) visit(resource->packed_depth_source->generation);
  };
  texture(command.resolve_destination);
  texture(command.depth_handoff_source);
  texture(command.present_source);
  texture(command.postfx_half_scene);
  for (const auto& resource : command.textures) texture(resource);
}

// Queue lock owns this index. Counts are logical references, not shared_ptrs or
// GPU ownership. Every zero count is removed immediately. If an invariant is
// violated the renderer falls back to its original queue scan, never guessing
// that an image is safe to retire.
class NativeTextureProtectionIndex {
 public:
  bool Retain(uint64_t generation) {
    if (!generation) return true;
    uint64_t& count = values_[Slot(generation, true)];
    if (count == UINT64_MAX) { valid_ = false; return false; }
    if (!count) ++live_;
    ++count; return true;
  }
  // Zero counts stay in the table (the same textures are retained and
  // released for nearly every draw) and are pruned in bulk.
  bool Release(uint64_t generation) {
    if (!generation) return true;
    const size_t slot = Slot(generation, false);
    if (slot == kNone || !values_[slot]) { valid_ = false; return false; }
    if (!--values_[slot]) --live_;
    return true;
  }
  void AppendTo(std::unordered_set<uint64_t>& destination) const {
    destination.reserve(destination.size() + live_);
    for (size_t i = 0; i < keys_.size(); ++i)
      if (keys_[i] && values_[i]) destination.insert(keys_[i]);
  }
  bool Contains(uint64_t generation) const {
    const size_t slot = Find(generation);
    return slot != kNone && values_[slot];
  }
  size_t size() const { return live_; }
  bool valid() const { return valid_; }
  void Reset() { keys_.clear(); values_.clear(); used_ = 0; live_ = 0; valid_ = true; }

 private:
  // Open addressing over two flat arrays; key 0 marks an empty slot (0 is
  // never a texture generation). No per-insert allocation, no node chasing.
  static constexpr size_t kNone = SIZE_MAX;
  static size_t Hash(uint64_t key) { return size_t((key * 0x9E3779B97F4A7C15ull) >> 17); }
  size_t Find(uint64_t key) const {
    if (keys_.empty() || !key) return kNone;
    const size_t mask = keys_.size() - 1;
    for (size_t i = Hash(key) & mask;; i = (i + 1) & mask) {
      if (keys_[i] == key) return i;
      if (!keys_[i]) return kNone;
    }
  }
  size_t Slot(uint64_t key, bool insert) {
    if (!insert) return Find(key);
    if ((used_ + 1) * 2 > keys_.size()) Rehash();
    const size_t mask = keys_.size() - 1;
    for (size_t i = Hash(key) & mask;; i = (i + 1) & mask) {
      if (keys_[i] == key) return i;
      if (!keys_[i]) { keys_[i] = key; values_[i] = 0; ++used_; return i; }
    }
  }
  // Grows, and drops zero-count entries, keeping the load factor under 1/2.
  void Rehash() {
    size_t capacity = 1024;
    while (capacity < (live_ + 1) * 4) capacity *= 2;
    std::vector<uint64_t> keys(capacity, 0), values(capacity, 0);
    const size_t mask = capacity - 1;
    for (size_t i = 0; i < keys_.size(); ++i) {
      if (!keys_[i] || !values_[i]) continue;
      size_t j = Hash(keys_[i]) & mask;
      while (keys[j]) j = (j + 1) & mask;
      keys[j] = keys_[i]; values[j] = values_[i];
    }
    keys_.swap(keys); values_.swap(values);
    used_ = live_;
  }
  std::vector<uint64_t> keys_;
  std::vector<uint64_t> values_;
  size_t used_ = 0;  // occupied slots, including zero counts
  size_t live_ = 0;  // slots with a nonzero count
  bool valid_ = true;
};
}  // namespace rex::graphics::gta4_native
