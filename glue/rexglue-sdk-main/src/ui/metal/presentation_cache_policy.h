#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <tuple>

namespace rex::ui::metal {

// Pixel identity only. UI invalidation and presentation pacing remain owned by
// Presenter, and UI commands are never stored in the cached game image.
struct PresentationImageKey {
  struct Effect {
    uint32_t kind = 0, width = 0, height = 0;
    bool operator==(const Effect&) const = default;
  };
  uint64_t publication = 0, surface_epoch = 0, texture_version = 0;
  uintptr_t source = 0;
  uint64_t source_format = 0, output_format = 0;
  uint32_t source_width = 0, source_height = 0;
  uint32_t output_width = 0, output_height = 0;
  uint32_t frontbuffer_width = 0, frontbuffer_height = 0;
  uint32_t aspect_x = 0, aspect_y = 0;
  uint32_t effect = 0, fsr_quality = 0, fsr_passes = 0;
  // The maximum FSR chain includes four EASU passes, RCAS and bilinear.
  // SetFlow statically checks that the presenter's capacity still fits here.
  std::array<Effect, 6> flow_effects{};
  uint32_t flow_effect_count = 0;
  int32_t flow_x = 0, flow_y = 0;
  float cas_sharpness = 0, fsr_sharpness = 0;
  uint32_t hdr_mode = 0, output_mode = 0;
  // Headroom, paper white, peak, shoulder start, shoulder power.
  std::array<float, 5> hdr{};
  bool active = false, is_8bpc = false, overscan = false, dither = false;

  template <typename Flow>
  void SetFlow(const Flow& flow) {
    static_assert(std::tuple_size_v<decltype(flow.effects)> <= std::tuple_size_v<decltype(flow_effects)>);
    flow_effects = {};
    flow_effect_count = uint32_t(flow.effect_count);
    // An inactive flow leaves its geometry unspecified; do not inspect it.
    flow_x = flow.effect_count ? flow.output_x : 0;
    flow_y = flow.effect_count ? flow.output_y : 0;
    for (std::size_t i = 0; i < flow.effect_count; ++i)
      flow_effects[i] = {uint32_t(flow.effects[i]), flow.effect_output_sizes[i].first,
                         flow.effect_output_sizes[i].second};
  }

  bool Cacheable() const {
    return active && publication && source && source_width && source_height &&
        output_width && output_height;
  }
  bool operator==(const PresentationImageKey&) const = default;
};

class PresentationCachePolicy {
 public:
  enum class Action { kDirect, kPopulate, kReuse };

  Action Inspect(const PresentationImageKey& key) const {
    if (!key.Cacheable()) return Action::kDirect;
    if (cached_ && *cached_ == key) return Action::kReuse;
    // A stream with one presentation per game frame keeps the original direct
    // path. Allocate and process an offscreen image only after a repeat occurs.
    return previous_ && *previous_ == key ? Action::kPopulate : Action::kDirect;
  }

  // Call only after the command buffer was committed. A cancelled recording
  // must never advertise pixels that were not submitted to the GPU.
  void Submitted(const PresentationImageKey& key, Action action) {
    previous_ = key;
    if (action == Action::kPopulate && key.Cacheable()) cached_ = key;
    else if (action == Action::kDirect) cached_.reset();
  }
  void InvalidatePixels() { cached_.reset(); }
  void Reset() { previous_.reset(); cached_.reset(); }

 private:
  std::optional<PresentationImageKey> previous_, cached_;
};

}  // namespace rex::ui::metal
