#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace rex::graphics::gta4_native::temporal {
// Identity comes from the executed model/pose owner, not submission order. A
// geometry generation changes when its captured vertex/index data is replaced.
struct GeometryKey {
  uint64_t instance = 0, drawable = 0, pose = 0, vertex_shader = 0, layout = 0,
           geometry_generation = 0, range = 0;
  struct Stream {
    uint64_t generation = 0;
    uint32_t offset = 0, stride = 0;
    bool operator==(const Stream&) const = default;
  };
  std::array<Stream, 17> streams{};
  uint64_t pixel_shader = 0, index_generation = 0;
  int32_t base_vertex = 0;
  uint32_t primitive = 0, restart_index = 0;
  bool indexed = false, restart = false;
  bool operator==(const GeometryKey&) const = default;
};
struct GeometryKeyHash {
  size_t operator()(const GeometryKey& k) const noexcept {
    uint64_t h = 0xcbf29ce484222325ull;
    const auto add = [&](uint64_t v) {
      v ^= v >> 30;
      v *= 0xbf58476d1ce4e5b9ull;
      v ^= v >> 27;
      v *= 0x94d049bb133111ebull;
      h ^= v ^ (v >> 31);
      h *= 0x100000001b3ull;
    };
    for (uint64_t v : {k.instance, k.drawable, k.pose, k.vertex_shader, k.layout,
                       k.geometry_generation, k.range}) add(v);
    for (const auto& stream : k.streams) {
      add(stream.generation); add(stream.offset); add(stream.stride);
    }
    add(k.pixel_shader); add(k.index_generation); add(uint32_t(k.base_vertex));
    add(k.primitive); add(k.restart_index); add(k.indexed); add(k.restart);
    return size_t(h);
  }
};
struct GeometryBudget {
  std::atomic<size_t> used{0};
  size_t limit = 0;
};
struct GeometrySnapshot {
  std::vector<uint8_t> vertex, shared;
  std::array<float, 2> jitter_clip{};
  uint64_t sequence = 0;
  std::shared_ptr<GeometryBudget> budget;
  ~GeometrySnapshot() {
    if (budget) budget->used.fetch_sub(vertex.size() + shared.size(), std::memory_order_relaxed);
  }
};
struct GeometryLookup {
  std::shared_ptr<const GeometrySnapshot> previous;
  bool captured = false, ambiguous = false;
};
class GeometryHistory {
 public:
  explicit GeometryHistory(size_t maximum_bytes = 64u * 1024u * 1024u,
                           size_t maximum_entries = 8192)
      : budget_(std::make_shared<GeometryBudget>()), maximum_entries_(maximum_entries) {
    budget_->limit = maximum_bytes;
  }
  bool Begin(uint64_t epoch, uint64_t view, uint64_t sequence, bool reset) {
    if (!epoch || !view || !sequence || epoch < epoch_) return false;
    if (epoch == epoch_ && view == view_ && sequence == sequence_) return !reset;
    if (epoch == epoch_ && view == view_ && sequence < sequence_) return false;
    const bool continuous = !reset && epoch == epoch_ && view == view_ && sequence_ != UINT64_MAX &&
                            sequence == sequence_ + 1;
    previous_.clear();
    if (continuous)
      previous_.swap(current_);
    else
      current_.clear();
    epoch_ = epoch;
    view_ = view;
    sequence_ = sequence;
    return true;
  }
  GeometryLookup Record(const GeometryKey& key, std::span<const uint8_t> vertex,
                        std::span<const uint8_t> shared, std::array<float, 2> jitter) {
    if (!sequence_ || !key.instance || !key.vertex_shader || !key.geometry_generation ||
        vertex.size() != 4096 || shared.empty() || shared.size() > 2048)
      return {};
    if (auto existing = current_.find(key); existing != current_.end()) {
      const auto& stored = *existing->second.snapshot;
      const bool same = stored.vertex.size() == vertex.size() &&
                        stored.shared.size() == shared.size() && stored.jitter_clip == jitter &&
                        !std::memcmp(stored.vertex.data(), vertex.data(), vertex.size()) &&
                        !std::memcmp(stored.shared.data(), shared.data(), shared.size());
      existing->second.ambiguous |= !same;
      if (existing->second.ambiguous) return {{}, true, true};
      return {Previous(key), true, false};
    }
    if (current_.size() >= maximum_entries_) return {};
    const size_t bytes = vertex.size() + shared.size(),
                 used = budget_->used.load(std::memory_order_relaxed);
    if (used > budget_->limit || bytes > budget_->limit - used) return {};
    auto snapshot = std::make_shared<GeometrySnapshot>();
    snapshot->vertex.assign(vertex.begin(), vertex.end());
    snapshot->shared.assign(shared.begin(), shared.end());
    snapshot->sequence = sequence_;
    snapshot->jitter_clip = jitter;
    budget_->used.fetch_add(bytes, std::memory_order_relaxed);
    snapshot->budget = budget_;
    current_.emplace(key, Entry{snapshot, false});
    return {Previous(key), true, false};
  }
  void Reset() {
    current_.clear();
    previous_.clear();
    epoch_ = view_ = sequence_ = 0;
  }
  size_t owned_bytes() const { return budget_->used.load(std::memory_order_relaxed); }
  size_t current_entries() const { return current_.size(); }

 private:
  struct Entry {
    std::shared_ptr<GeometrySnapshot> snapshot;
    bool ambiguous = false;
  };
  std::shared_ptr<const GeometrySnapshot> Previous(const GeometryKey& key) const {
    const auto old = previous_.find(key);
    return old == previous_.end() || old->second.ambiguous ? nullptr : old->second.snapshot;
  }
  std::unordered_map<GeometryKey, Entry, GeometryKeyHash> previous_, current_;
  std::shared_ptr<GeometryBudget> budget_;
  size_t maximum_entries_;
  uint64_t epoch_ = 0, view_ = 0, sequence_ = 0;
};
}  // namespace rex::graphics::gta4_native::temporal
