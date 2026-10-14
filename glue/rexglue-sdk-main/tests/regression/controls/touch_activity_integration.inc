// Included by touch_runtime.cpp: exercise the actual pointer/freeze/query path.
#include "touch_activity_samples.h"
#include <rex/input/pointer_clock.h>
namespace {
size_t ActivityProfile(TouchActivityKind kind, uint32_t episode = 0) {
  for (size_t i=0;i<kTouchActivityProfiles.size();++i)
    if (kTouchActivityProfiles[i].kind==kind && kTouchActivityProfiles[i].episode==episode) return i;
  assert(false); return 0;
}
void SetActivity(TouchActivityKind kind, std::array<uint32_t,8> state, uint32_t episode = 0) {
  facts.activity=ClassifyTouchActivity(ActivityProfile(kind,episode),state,true,false);
  assert(facts.activity.valid); facts.activity.script_thread=71;
  facts.minigame_active=true; facts.gameplay_allowed=false; Poll();
}
std::array<int32_t,4> ActivityAxes() {
  std::array<int32_t,4> result{};
  GetTouchScriptAnalogueSticks(facts.epoch,&result,0,71,facts.generation);
  return result;
}
void ActivityTransactions() {
  using namespace activity_samples;
  using K=TouchActivityKind; using P=TouchActivityPhase; using Q=TouchScriptQueryKind;
  Reset(); Arm(); const auto fire=Control(A::kFire); Send(800,Phase::kDown,fire); assert(Freeze().right_trigger);
  SetActivity(K::kBowling,{4,3});
  assert(g_runtime.pointers.empty());
  for (size_t i=0;i<g_runtime.layout.control_count;++i)
    assert(!g_runtime.layout.controls[i].visible ||
        (g_runtime.layout.controls[i].action!=A::kFire && g_runtime.layout.controls[i].action!=A::kWeaponWheel));
  assert(!OnPointerEvent(Event(800,Phase::kUp,fire.center_x,fire.center_y),ppc,guest,facts.epoch));
  const auto stroke=Control(A::kActivityPrimary); const auto start=MonotonicNanoseconds();
  assert(OnPointerEvent(Event(801,Phase::kDown,stroke.center_x,stroke.center_y,start),ppc,guest,facts.epoch));
  assert(OnPointerEvent(Event(801,Phase::kMove,stroke.center_x,stroke.center_y+kMovePixels,start+kMoveNsDelta),ppc,guest,facts.epoch));
  assert(OnPointerEvent(Event(801,Phase::kMove,stroke.center_x,stroke.center_y-kMovePixels,start+kReverseNsDelta),ppc,guest,facts.epoch));
  assert(OnPointerEvent(Event(801,Phase::kUp,stroke.center_x,stroke.center_y-kMovePixels,start+kReverseNsDelta),ppc,guest,facts.epoch));
  auto pad=Freeze(); assert(pad.buttons==0 && pad.left_trigger==0 && pad.right_trigger==0 && pad.left_x==0 && pad.right_y==0);
  auto axes=ActivityAxes(); assert(axes[3]>0 && axes==ActivityAxes());
  std::array<int32_t,4> other{};
  assert(!GetTouchScriptAnalogueSticks(facts.epoch,&other,0,72,facts.generation));
  assert(!GetTouchScriptAnalogueSticks(facts.epoch,&other,1,71,facts.generation));
  Poll(); Freeze(); assert(ActivityAxes()[3]<0);
  Poll(); Freeze(); assert(ActivityAxes()==other);
  Poll(); const auto host_now=MonotonicNanoseconds();
  const auto rebase=[&](uint64_t source) { return rex::input::RebasePointerTimestamp(source,kReverseNs,host_now); };
  Send(807,Phase::kDown,stroke.center_x,stroke.center_y,rebase(kBeginNs));
  Send(807,Phase::kMove,stroke.center_x,stroke.center_y+kMovePixels,rebase(kMoveNs));
  Send(807,Phase::kMove,stroke.center_x,stroke.center_y-kMovePixels,rebase(kReverseNs));
  Send(807,Phase::kUp,stroke.center_x,stroke.center_y-kMovePixels,rebase(kReverseNs));
  Freeze(); assert(ActivityAxes()[3]>0);
  Poll(); Freeze(); assert(ActivityAxes()[3]<0);
  Poll(); Freeze(); assert(ActivityAxes()==other);
  std::cout << "PASS SDL-relative event timestamps are rebased before gesture expiry and preserve both stroke directions\n";
  std::cout << "PASS specialized activity takes ownership immediately; stroke history reaches only its original script and never the native pad\n";
  Poll(); Send(802,Phase::kDown,stroke);
  assert(OnPointerEvent(Event(802,Phase::kMove,stroke.center_x,stroke.center_y+kMovePixels,MonotonicNanoseconds()),ppc,guest,facts.epoch));
  SetActivity(K::kBowling,{4,18}); Freeze(); assert(g_runtime.pointers.empty() && ActivityAxes()==other);
  assert(!OnPointerEvent(Event(802,Phase::kUp,stroke.center_x,stroke.center_y),ppc,guest,facts.epoch));
  std::cout << "PASS phase transition discards queued strokes and cannot transfer an old finger into aftertouch\n";
  SetActivity(K::kHiLo,{4,5},1);
  const auto layout=GetContextTouchOverlaySnapshot().layout;
  const ContextTouchControl* choice=nullptr;
  for(size_t i=0;i<layout.control_count;++i) if(layout.controls[i].script.action==79) choice=&layout.controls[i];
  assert(choice); Send(803,Phase::kDown,*choice); Send(803,Phase::kUp,*choice); Freeze();
  assert(GetTouchScriptQueryValue(Q::kControlHeld,79,facts.epoch,2,71,facts.generation)==1);
  assert(GetTouchScriptQueryValue(Q::kControlPressed,79,facts.epoch,2,71,facts.generation)==1);
  assert(GetTouchScriptQueryValue(Q::kControlHeld,79,facts.epoch,2,72,facts.generation)==0);
  Poll(); Freeze(); assert(GetTouchScriptQueryValue(Q::kControlHeld,79,facts.epoch,2,71,facts.generation)==0);
  Poll(); Send(804,Phase::kDown,*choice); Send(804,Phase::kCancel,*choice); Freeze();
  assert(GetTouchScriptQueryValue(Q::kControlPressed,79,facts.epoch,2,71,facts.generation)==0);
  std::cout << "PASS complete choice taps survive held-query consumers for exactly one poll; cancellation removes every edge\n";
  SetActivity(K::kDancing,{0,99},2); const auto dance=Control(A::kActivityPrimary);
  Send(805,Phase::kDown,dance); Send(805,Phase::kMove,dance.center_x+kMovePixels,dance.center_y); Freeze(); const auto held=ActivityAxes(); assert(held[0]>0);
  SetActivity(K::kDancing,{1,0},2); Freeze(); assert(g_runtime.pointers.contains(805) && ActivityAxes()==held);
  SetActivity(K::kDancing,{10,0},2); Freeze(); assert(!g_runtime.pointers.contains(805) && ActivityAxes()==other);
  std::cout << "PASS dance hold preserves actual thumb deflection without auto-holding; group-dance transition clears it\n";
  Reset(); InitializeContextTouchControls(); Arm(); const auto now=MonotonicNanoseconds();
  GetContextTouchDrawableOverlaySnapshot(now); GetContextTouchDrawableOverlaySnapshot(now+ContextTouchFade::kDurationNanoseconds);
  SetActivity(K::kBowling,{4,15}); const auto changed=g_runtime.layout_transition_started_ns;
  auto visual=GetContextTouchDrawableOverlaySnapshot(changed);
  assert(visual.visible && visual.outgoing_layout && visual.outgoing_alpha==1.0f && visual.layout_alpha==0.0f);
  const auto position=Control(A::kActivityPrimary);
  const auto pointer_now=MonotonicNanoseconds();
  Send(806,Phase::kDown,position,pointer_now);
  Send(806,Phase::kMove,position.center_x+kMovePixels,position.center_y,pointer_now+kMoveNsDelta);
  Freeze();
  assert(ActivityAxes()[0]>0);
  visual=GetContextTouchDrawableOverlaySnapshot(changed+kCrossfadeHalfNs);
  assert(visual.outgoing_layout && visual.outgoing_alpha==0.5f && visual.layout_alpha==0.5f);
  visual=GetContextTouchDrawableOverlaySnapshot(changed+kCrossfadeDoneNs);
  assert(!visual.outgoing_layout && visual.layout_alpha==1.0f);
  SetActivity(K::kBowling,{4,3}); assert(g_runtime.outgoing_layout);
  SuspendContextTouchGameplay(); assert(!g_runtime.outgoing_layout && !GetContextTouchDrawableOverlaySnapshot().visible);
  std::cout << "PASS layout crossfade is presentation-only, accepts new input immediately, and releases retained layouts on completion or suspension\n";
}
void NativeWeaponSelectorGestures() {
  using namespace activity_samples;
  Reset(); InitializeContextTouchControls(); Arm(); facts.inventory_known=true;
  facts.weapons[0].selectable=true; facts.weapons[0].owned=true;
  facts.weapons[2].selectable=true; facts.weapons[2].owned=true; facts.weapons[2].type=7;
  std::strcpy(facts.weapons[2].native_identifier.data(),"PISTOL");
  facts.weapon_hud_bounds=ContextTouchHudBounds{0.80f,0.03f,0.96f,0.13f}; Poll();
  auto selector=Control(A::kWeaponWheel); assert(selector.native_hud && ContextTouchWeaponHudSelectorActive());
  const auto bounds=MapContextTouchHudBounds(*facts.weapon_hud_bounds,g_runtime.layout.viewport); assert(bounds);
  assert(selector.minimum_x==bounds->left && selector.maximum_y==bounds->bottom);
  Send(900,Phase::kDown,selector); Send(900,Phase::kUp,selector); assert(Freeze().buttons==rex::input::X_INPUT_GAMEPAD_DPAD_RIGHT);
  Poll(); auto timestamp=MonotonicNanoseconds();
  assert(OnPointerEvent(Event(901,Phase::kDown,selector.center_x,selector.center_y,timestamp),ppc,guest,facts.epoch));
  assert(!OnPointerEvent(Event(902,Phase::kDown,selector.center_x,selector.center_y,timestamp),ppc,guest,facts.epoch));
  assert(OnPointerEvent(Event(901,Phase::kMove,selector.center_x,selector.center_y,timestamp+kWeaponWheelHoldNanoseconds),ppc,guest,facts.epoch));
  assert(g_runtime.weapon_wheel_open); const auto wheel=g_runtime.layout;
  const ContextTouchControl* fists=nullptr;
  for(size_t i=0;i<wheel.control_count;++i) if(wheel.controls[i].action==A::kWeaponSelect && wheel.controls[i].weapon_slot==0) fists=&wheel.controls[i];
  assert(fists);
  assert(OnPointerEvent(Event(901,Phase::kUp,fists->center_x,fists->center_y,timestamp+kWeaponWheelHoldNanoseconds),ppc,guest,facts.epoch));
  assert(!g_runtime.weapon_wheel_open && g_runtime.weapon_target==0);
  assert(Freeze().buttons!=0 && facts.weapon_slot==2);
  facts.weapon_slot=0; Poll(); assert(Freeze().buttons==0 && g_runtime.weapon_target==kNoWeaponSelection);
  Poll(); selector=Control(A::kWeaponWheel);
  Send(903,Phase::kDown,selector); Send(903,Phase::kUp,selector.center_x-selector.radius,selector.center_y);
  assert(Freeze().buttons==rex::input::X_INPUT_GAMEPAD_DPAD_LEFT);
  Poll(); Send(904,Phase::kDown,selector); Send(904,Phase::kCancel,selector); assert(Freeze().buttons==0);
  std::cout << "PASS native current-weapon silhouette has exclusive tap/swipe/hold selection, no duplicate drawing, and guest-acknowledged equipment changes\n";
}
}
