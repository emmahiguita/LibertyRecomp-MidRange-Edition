#include "graphics/gta4_metal/render_phase_stack.h"

#include <cstdio>
#include <stdexcept>
#include <string>

using rex::graphics::gta4_metal::RenderPhaseStack;
using rex::graphics::gta4_native::RenderPhase;
using rex::graphics::gta4_native::RenderPhaseEvent;
using rex::graphics::gta4_native::RenderPhaseMarkerCommand;
using Result = RenderPhaseStack::Result;

namespace {
size_t checks = 0;
void Check(bool value, const char* message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
RenderPhaseMarkerCommand Marker(RenderPhase phase, RenderPhaseEvent event,
                               uint32_t object, uint32_t half_scene = 0) {
  RenderPhaseMarkerCommand result{};
  result.phase = phase; result.event = event;
  result.object = object; result.half_scene_texture = half_scene;
  return result;
}
void State(const RenderPhaseStack& stack, RenderPhase phase, size_t depth, uint32_t half_scene) {
  Check(stack.phase() == phase, "Incorrect surviving phase");
  Check(stack.depth() == depth, "Incorrect surviving scope count");
  Check(stack.half_scene_texture() == half_scene, "Incorrect surviving half-scene texture");
}
void Apply(RenderPhaseStack& stack, RenderPhase phase, RenderPhaseEvent event,
           uint32_t object, Result expected, uint32_t half_scene = 0) {
  Check(stack.Apply(Marker(phase, event, object, half_scene)) == expected,
        "Incorrect phase transition result");
}
}  // namespace

int main() {
  try {
    constexpr auto begin = RenderPhaseEvent::kBegin, end = RenderPhaseEvent::kEnd;
    constexpr auto scene = RenderPhase::kSceneToGBuffer, radar = RenderPhase::kRadarMap;
    constexpr auto composite = RenderPhase::kCompositePostFx, unknown = RenderPhase::kUnknown;
    RenderPhaseStack stack;
    State(stack, unknown, 0, 0);

    // Normal nesting preserves the exact outer scope, including same-phase scopes.
    Apply(stack, scene, begin, 10, Result::kApplied);
    Apply(stack, scene, begin, 20, Result::kApplied);
    Apply(stack, scene, end, 20, Result::kApplied);
    State(stack, scene, 1, 0);
    Apply(stack, scene, end, 10, Result::kApplied);
    State(stack, unknown, 0, 0);

    // Every end matches both phase and object. Mismatches never pop a different scope.
    Apply(stack, scene, begin, 10, Result::kApplied);
    Apply(stack, scene, end, 20, Result::kMismatch);
    State(stack, unknown, 0, 0);
    Apply(stack, scene, begin, 10, Result::kApplied);
    Apply(stack, radar, end, 10, Result::kMismatch);
    State(stack, unknown, 0, 0);
    Apply(stack, scene, end, 10, Result::kMismatch);
    State(stack, unknown, 0, 0);

    // Balanced scopes on two submitters can overlap without nesting globally.
    // Repeat beyond the old bound: this used to leak one scope each iteration.
    for (size_t i = 0; i < 1024; ++i) {
      Apply(stack, scene, begin, 10, Result::kApplied);  // Thread A.
      Apply(stack, radar, begin, 20, Result::kApplied); // Thread B.
      Apply(stack, scene, end, 10, Result::kMismatch);  // Thread A.
      State(stack, unknown, 0, 0);
      Apply(stack, radar, end, 20, Result::kMismatch);  // Thread B.
      State(stack, unknown, 0, 0);
    }

    // A nested composite restores the outer texture; intervening phases do not erase it.
    Apply(stack, composite, begin, 10, Result::kApplied, 100);
    Apply(stack, radar, begin, 20, Result::kApplied, 999);
    State(stack, radar, 2, 100);
    Apply(stack, radar, end, 20, Result::kApplied);
    Apply(stack, composite, begin, 30, Result::kApplied, 300);
    State(stack, composite, 2, 300);
    Apply(stack, composite, end, 30, Result::kApplied, 999);
    State(stack, composite, 1, 100);
    // A valid inner composite with no half-scene must not borrow its outer texture.
    Apply(stack, composite, begin, 40, Result::kApplied);
    State(stack, composite, 2, 0);
    Apply(stack, composite, end, 40, Result::kApplied);
    State(stack, composite, 1, 100);

    // Invalid fields reject before mutating phase or resource metadata.
    Apply(stack, unknown, begin, 10, Result::kInvalidPhase, 999);
    State(stack, composite, 1, 100);
    Apply(stack, static_cast<RenderPhase>(999), end, 10, Result::kInvalidPhase, 999);
    State(stack, composite, 1, 100);
    Apply(stack, composite, static_cast<RenderPhaseEvent>(999), 10, Result::kInvalidEvent, 999);
    State(stack, composite, 1, 100);
    Apply(stack, composite, end, 11, Result::kMismatch);
    State(stack, unknown, 0, 0);

    // Exact bound is valid. The rejected next begin must discard all stale metadata.
    for (size_t i = 0; i < RenderPhaseStack::kMaximumDepth; ++i)
      Apply(stack, composite, begin, uint32_t(i), Result::kApplied, uint32_t(i + 1));
    State(stack, composite, RenderPhaseStack::kMaximumDepth, uint32_t(RenderPhaseStack::kMaximumDepth));
    Apply(stack, composite, begin, 999, Result::kOverflow, 999);
    State(stack, unknown, 0, 0);
    Apply(stack, composite, end, 999, Result::kMismatch);
    Apply(stack, composite, begin, 50, Result::kApplied, 500);
    State(stack, composite, 1, 500);
    Apply(stack, composite, end, 50, Result::kApplied);
    State(stack, unknown, 0, 0);

    // Device destruction explicitly resets both attribution and resource metadata.
    Apply(stack, composite, begin, 60, Result::kApplied, 600);
    stack.Reset();
    State(stack, unknown, 0, 0);

    std::printf("metal_render_phase_stack=passed checks=%zu interleavings=1024\n", checks);
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "Metal phase stack test: %s\n", error.what());
    return 1;
  }
}
