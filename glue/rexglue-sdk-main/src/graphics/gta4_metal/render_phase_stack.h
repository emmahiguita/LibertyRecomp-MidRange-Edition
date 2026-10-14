#pragma once

#include <rex/graphics/gta4_native/title_commands.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rex::graphics::gta4_metal {

// Submission locking does not make scopes on different guest threads nest.
// Recover an unbalanced stream to Unknown instead of retaining stale phases.
class RenderPhaseStack {
 public:
  static constexpr size_t kMaximumDepth = 32;
  enum class Result { kApplied, kMismatch, kOverflow, kInvalidPhase, kInvalidEvent };

  Result Apply(const gta4_native::RenderPhaseMarkerCommand& marker) {
    using gta4_native::RenderPhase;
    using gta4_native::RenderPhaseEvent;
    if (marker.phase <= RenderPhase::kUnknown || marker.phase > RenderPhase::kCompositePostFx)
      return Result::kInvalidPhase;
    if (marker.event != RenderPhaseEvent::kBegin && marker.event != RenderPhaseEvent::kEnd)
      return Result::kInvalidEvent;
    if (marker.event == RenderPhaseEvent::kBegin) {
      if (scopes_.size() >= kMaximumDepth) {
        Reset();
        return Result::kOverflow;
      }
      scopes_.push_back({marker.phase, marker.object,
                        marker.phase == RenderPhase::kCompositePostFx ? marker.half_scene_texture : 0});
      return Result::kApplied;
    }
    if (scopes_.empty() || scopes_.back().phase != marker.phase ||
        scopes_.back().object != marker.object) {
      Reset();
      return Result::kMismatch;
    }
    scopes_.pop_back();
    return Result::kApplied;
  }

  void Reset() { scopes_.clear(); }
  size_t depth() const { return scopes_.size(); }
  gta4_native::RenderPhase phase() const {
    return scopes_.empty() ? gta4_native::RenderPhase::kUnknown : scopes_.back().phase;
  }
  uint32_t half_scene_texture() const {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope)
      if (scope->phase == gta4_native::RenderPhase::kCompositePostFx)
        return scope->half_scene_texture;
    return 0;
  }

 private:
  struct Scope {
    gta4_native::RenderPhase phase;
    uint32_t object;
    uint32_t half_scene_texture;
  };
  std::vector<Scope> scopes_;
};

}  // namespace rex::graphics::gta4_metal
