#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

namespace rex::graphics::gta4_metal {
// Immutable uploads from one command buffer only. Dense entries and bucket
// storage survive Clear(), avoiding a map node and collision-vector allocation
// for every changed constant bank. Hash matches always receive an exact check.
template<class Upload> class ConstantUploadCache {
 public:
  explicit ConstantUploadCache(size_t source_budget = 8u * 1024u * 1024u)
      : source_budget_(source_budget) {}
  void Clear() {
    last_source_=nullptr;
    if (entries_.empty()) return;
    entries_.clear();  // Release upload ownership before frame-slot recycling.
    std::fill(buckets_.begin(), buckets_.end(), kNone);
    source_size_ = 0;
  }
  const Upload* Find(uint64_t hash, std::span<const uint8_t> bytes, bool swapped) const {
    assert(!swapped || bytes.size() % sizeof(uint32_t) == 0);
    last_source_=nullptr;
    if (buckets_.empty()) return nullptr;
    for (size_t index = buckets_[hash & (buckets_.size() - 1)]; index != kNone;
         index = entries_[index].next) {
      const auto& entry = entries_[index];
      if (entry.hash != hash || entry.size != bytes.size() || entry.swapped != swapped) continue;
      const auto* source = entry.source_offset != kNone
          ? sources_.get() + entry.source_offset : static_cast<const uint8_t*>(entry.upload.data);
      if (entry.source_offset != kNone || !swapped) {
        if (!std::memcmp(source, bytes.data(), bytes.size())) {last_source_=entry.source_offset!=kNone?source:nullptr;return &entry.upload;}
      } else if (EqualGuestWords(source, bytes)) return &entry.upload;
    }
    return nullptr;
  }
  const uint8_t* Insert(uint64_t hash, std::span<const uint8_t> bytes, bool swapped,
              const Upload& upload, bool cache_sources) {
    assert(!swapped || bytes.size() % sizeof(uint32_t) == 0);
    if (entries_.size() >= buckets_.size()) Grow();
    size_t source_offset = kNone;
    if (cache_sources && swapped && bytes.size() <= source_budget_ - source_size_) {
      if (!sources_) sources_.reset(new uint8_t[source_budget_]);
      source_offset = source_size_;
      std::memcpy(sources_.get() + source_offset, bytes.data(), bytes.size());
      source_size_ += bytes.size();
    }
    auto& bucket = buckets_[hash & (buckets_.size() - 1)];
    entries_.push_back({hash, bytes.size(), upload, source_offset, bucket, swapped});
    bucket = entries_.size() - 1;
    return source_offset==kNone?nullptr:sources_.get()+source_offset;
  }
  const uint8_t* source_of_last_hit() const {return last_source_;}
  size_t size() const { return entries_.size(); }
  size_t source_bytes() const { return source_size_; }
 private:
  static constexpr size_t kNone = SIZE_MAX;
  struct Entry {
    uint64_t hash;
    size_t size;
    Upload upload;
    size_t source_offset, next;
    bool swapped;
  };
  static bool EqualGuestWords(const uint8_t* host, std::span<const uint8_t> guest) {
    // The source-copy budget can be exhausted in draw-heavy frames. Compare
    // converted blocks then the tail, rather than calling memcmp per word.
    // No floating-point operations: NaNs, signed zero, and packed bits survive.
    size_t offset = 0;
    std::array<uint32_t, 16> words;
    for (; guest.size() - offset >= sizeof(words); offset += sizeof(words)) {
      for (size_t i = 0; i < words.size(); ++i) {
        uint32_t value;
        std::memcpy(&value, guest.data() + offset + i * sizeof(value), sizeof(value));
        words[i] = __builtin_bswap32(value);
      }
      if (std::memcmp(host + offset, words.data(), sizeof(words))) return false;
    }
    for (; offset < guest.size(); offset += sizeof(uint32_t)) {
      uint32_t value;
      std::memcpy(&value, guest.data() + offset, sizeof(value));
      value = __builtin_bswap32(value);
      if (std::memcmp(host + offset, &value, sizeof(value))) return false;
    }
    return true;
  }
  void Grow() {
    buckets_.assign(buckets_.empty() ? 1024 : buckets_.size() * 2, kNone);
    entries_.reserve(buckets_.size());
    for (size_t i = 0; i < entries_.size(); ++i) {
      auto& bucket = buckets_[entries_[i].hash & (buckets_.size() - 1)];
      entries_[i].next = bucket;
      bucket = i;
    }
  }
  std::vector<Entry> entries_;
  std::vector<size_t> buckets_;
  std::unique_ptr<uint8_t[]> sources_;
  size_t source_budget_, source_size_ = 0;
  mutable const uint8_t* last_source_=nullptr;
};
}  // namespace rex::graphics::gta4_metal
