#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>
#include <rex/input/absolute_pointer.h>
#include <rex/input/pointer_clock.h>
#include "input/context_touch_activity.h"
#include "touch_activity_samples.h"

using namespace rex::input;
using namespace gta4::input;
using namespace activity_samples;

namespace {
rex::ui::GuestOutputTransform IdentityTransform() {
  rex::ui::GuestOutputTransform t;
  t.revision = 1;
  t.surface_width = t.host_render_target_width = t.output_width = t.guest_width = 1280;
  t.surface_height = t.host_render_target_height = t.output_height = t.guest_height = 720;
  return t;
}
std::vector<AbsolutePointerEvent> Drain(AbsolutePointerService& service) {
  std::vector<AbsolutePointerEvent> events;
  AbsolutePointerEvent event;
  for (; service.TryDequeue(&event);) events.push_back(event);
  return events;
}
void Prepare(AbsolutePointerService& service) {
  const auto transform = IdentityTransform();
  service.UpdatePresentation(transform, 0, 0, 1280, 720, kBeginNs);
  service.SetLogicalSize(1280, 720, kBeginNs);
  service.SetFocused(true, kBeginNs);
  Drain(service);
}
void TransportedStroke() {
  auto& service = GetAbsolutePointerService();
  Prepare(service);
  service.SubmitPointer(42, 1, AbsolutePointerPhase::kDown, 320, 360, 1, kBeginNs);
  service.SubmitPointer(42, 1, AbsolutePointerPhase::kMove, 320, 440, 1, kMoveNs);
  service.SubmitPointer(42, 1, AbsolutePointerPhase::kMove, 320, 280, 1, kReverseNs);
  service.SubmitPointer(42, 1, AbsolutePointerPhase::kUp, 320, 280, 0, kReverseNs);
  const auto events = Drain(service);
  assert(std::count_if(events.begin(), events.end(), [](const auto& e) {
    return e.phase == AbsolutePointerPhase::kMove;
  }) == 2);
  TouchActivityGestureState gesture;
  for (const auto& e : events) {
    if (e.phase == AbsolutePointerPhase::kDown)
      gesture.Begin(TouchActivityGesture::kStrokeRight, e.logical_x, e.logical_y,
                    e.logical_x, e.logical_y, kRadius, e.timestamp_ns);
    else if (e.phase == AbsolutePointerPhase::kMove)
      gesture.Move(e.logical_x, e.logical_y, e.timestamp_ns);
    else if (e.phase == AbsolutePointerPhase::kUp) gesture.Release(e.timestamp_ns);
    else gesture.Cancel();
  }
  assert(gesture.Sample(kReverseNs, kFrame).axes[3] == kStrokeForward);
  assert(gesture.Sample(kReverseNs, kFrame).axes[3] == kStrokeBackward);
  assert(!gesture.Sample(kReverseNs, kFrame).nonzero());
  assert(service.queued_move_count() == 0);
  std::cout << "PASS production pointer transport preserves a complete sub-poll backswing and forward stroke\n";
}
void OverflowCancelsIncompleteHistory() {
  AbsolutePointerService service(2, true);
  Prepare(service);
  service.SubmitPointer(42, 1, AbsolutePointerPhase::kDown, 320, 360, 1, kBeginNs);
  service.SubmitPointer(42, 2, AbsolutePointerPhase::kDown, 640, 360, 1, kBeginNs);
  const auto starts = Drain(service);
  assert(starts.size() == 2);
  service.SubmitPointer(42, 1, AbsolutePointerPhase::kMove, 320, 440, 1, kMoveNs);
  service.SubmitPointer(42, 2, AbsolutePointerPhase::kMove, 640, 440, 1, kMoveNs);
  assert(service.queued_move_count() == 2);
  service.SubmitPointer(42, 1, AbsolutePointerPhase::kMove, 320, 280, 1, kReverseNs);
  const auto ends = Drain(service);
  assert(service.queued_move_count() == 0 && ends.size() == 2);
  for (const auto& e : ends) assert(e.phase == AbsolutePointerPhase::kCancel);
  TouchPresentationState presentation;
  assert(service.GetPresentationState(&presentation));
  assert(presentation.generation > starts.front().generation);
  service.SubmitPointer(42, 1, AbsolutePointerPhase::kMove, 320, 280, 1, kReverseNs);
  service.SubmitPointer(42, 2, AbsolutePointerPhase::kUp, 640, 440, 0, kReverseNs);
  assert(Drain(service).empty());
  service.SubmitPointer(42, 1, AbsolutePointerPhase::kDown, 320, 360, 1, kReverseNs);
  const auto fresh = Drain(service);
  assert(fresh.size() == 1 && fresh.front().pointer_id != starts.front().pointer_id);
  assert(fresh.front().generation == presentation.generation);
  service.CancelAll(kReverseNs); Drain(service);
  std::cout << "PASS motion-history saturation cancels all incomplete contacts and requires a fresh down\n";
}
void LatestPositionCompatibility() {
  AbsolutePointerService service(2);
  Prepare(service);
  service.SubmitPointer(42, 1, AbsolutePointerPhase::kDown, 320, 360, 1, kBeginNs);
  service.SubmitPointer(42, 1, AbsolutePointerPhase::kMove, 320, 440, 1, kMoveNs);
  service.SubmitPointer(42, 1, AbsolutePointerPhase::kMove, 320, 280, 1, kReverseNs);
  service.SubmitPointer(42, 1, AbsolutePointerPhase::kUp, 320, 280, 0, kReverseNs);
  const auto events = Drain(service);
  assert(events.size() == 3 && events[1].phase == AbsolutePointerPhase::kMove);
  assert(events[1].logical_y == 280 && events.back().phase == AbsolutePointerPhase::kUp);
  std::cout << "PASS callers requesting latest-position transport retain existing coalescing and lifecycle behavior\n";
}
void ExpiredRelativeDelta() {
  for (auto kind : {TouchActivityGesture::kRelativeLeft, TouchActivityGesture::kRelativeRight,
                    TouchActivityGesture::kHorizontalLeft}) {
    TouchActivityGestureState g;
    g.Begin(kind, 0, 0, 0, 0, kRadius, kBeginNs);
    g.Move(kTravel, kTravel, kMoveNs);
    g.Move(kTravel, kTravel, kExpiredNs); // Stationary release cannot renew stale motion.
    g.Release(kExpiredNs);
    assert(!g.Sample(kExpiredNs, kFrame).nonzero());
    g.Begin(kind, 0, 0, 0, 0, kRadius, kBeginNs);
    g.Move(kTravel, kTravel, kMoveNs);
    assert(!g.Sample(kExpiredNs, kFrame).nonzero());
    g.Move(0, 0, kExpiredNs);
    assert(g.Sample(kExpiredNs, kFrame).axes[kind == TouchActivityGesture::kRelativeRight ? 2 : 0] < 0);
  }
  std::cout << "PASS delayed relative motion expires without renewing on a stationary release or losing later fresh motion\n";
}
}
int main() {
  TransportedStroke(); OverflowCancelsIncompleteHistory(); LatestPositionCompatibility(); ExpiredRelativeDelta();
}
