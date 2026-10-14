#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <rex/graphics/gta4_native/gpu_pass_origin.h>

namespace rex::graphics::gta4_native {

// Profiling metadata for one EXISTING render scope. No GPU state, resource
// ownership, allocation, timestamp query or render-scope transition lives here.
// The summaries describe issued draws; they do not estimate shader time from
// draw counts, index counts, or CPU encoding duration.
struct NativeGpuScopeKey {
  uint64_t vertex_shader = 0;
  uint64_t pixel_shader = 0;
  uint32_t retail_phase = UINT32_MAX;
  uint32_t semantic_phase = 0;
  GpuPassOriginSource source = GpuPassOriginSource::kNone;
  uint64_t shader_variant = 0;
  uint32_t samples = 0;
  bool operator==(const NativeGpuScopeKey&) const = default;
};

class NativeGpuScopeSummary {
 public:
  static constexpr size_t kCapacity = 64;
  struct Entry {
    NativeGpuScopeKey key{};
    std::string_view vertex_name;
    std::string_view pixel_name;
    std::string_view selected_vertex_name;
    std::string_view selected_pixel_name;
    uint64_t draws = 0, vertices = 0, indices = 0;
    uint64_t first_pipeline = 0, first_list_scope = 0, last_list_scope = 0;
    bool mixed_pipelines = false;
  };

  void Begin(uint32_t frame, uint32_t first_command) {
    frame_ = frame;
    first_command_ = first_command;
    ++serial_;
    size_ = 0;
    last_entry_ = kCapacity;
    draws_ = vertices_ = indices_ = clears_ = overflow_draws_ = 0;
    active_ = true;
  }

  void Observe(const NativeGpuScopeKey& key, std::string_view vertex_name,
               std::string_view pixel_name, uint64_t pipeline, uint64_t list_scope,
               uint32_t vertices, uint32_t indices,
               std::string_view selected_vertex_name = {},
               std::string_view selected_pixel_name = {}) {
    if (!active_ || (!vertices && !indices)) return;
    ++draws_;
    vertices_ += vertices;
    indices_ += indices;
    size_t slot = last_entry_;
    if (slot >= size_ || !(entries_[slot].key == key)) {
      slot = 0;
      for (; slot < size_ && !(entries_[slot].key == key); ++slot) {}
      if (slot == size_) {
        if (size_ == kCapacity) {
          ++overflow_draws_;
          last_entry_ = kCapacity;
          return;
        }
        entries_[size_++] = Entry{.key = key,
                                 .vertex_name = vertex_name,
                                 .pixel_name = pixel_name,
                                 .selected_vertex_name = selected_vertex_name.empty() ? vertex_name : selected_vertex_name,
                                 .selected_pixel_name = selected_pixel_name.empty() ? pixel_name : selected_pixel_name,
                                 .first_pipeline = pipeline,
                                 .first_list_scope = list_scope};
      }
    }
    last_entry_ = slot;
    Entry& entry = entries_[slot];
    ++entry.draws;
    entry.vertices += vertices;
    entry.indices += indices;
    entry.last_list_scope = list_scope;
    entry.mixed_pipelines |= entry.first_pipeline != pipeline;
  }

  void ObserveClear() { if (active_) ++clears_; }
  void End() { active_ = false; }
  bool active() const { return active_; }
  uint32_t frame() const { return frame_; }
  uint32_t first_command() const { return first_command_; }
  uint32_t serial() const { return serial_; }
  uint64_t draws() const { return draws_; }
  uint64_t vertices() const { return vertices_; }
  uint64_t indices() const { return indices_; }
  uint64_t clears() const { return clears_; }
  uint64_t overflow_draws() const { return overflow_draws_; }
  std::span<const Entry> entries() const { return {entries_.data(), size_}; }

 private:
  // Names refer to shader resources already owned by the current frame. The
  // summary is consumed before that frame releases any command/resource owners.
  std::array<Entry, kCapacity> entries_{};
  size_t size_ = 0, last_entry_ = kCapacity;
  uint64_t draws_ = 0, vertices_ = 0, indices_ = 0, clears_ = 0, overflow_draws_ = 0;
  uint32_t frame_ = 0, first_command_ = 0, serial_ = 0;
  bool active_ = false;
};

}  // namespace rex::graphics::gta4_native
