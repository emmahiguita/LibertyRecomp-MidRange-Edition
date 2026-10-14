#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>

#include "input/context_touch_activity.h"
#include "input/context_touch_context.h"
#include "touch_activity_samples.h"

using namespace gta4::input;
using namespace activity_samples;
using K = TouchActivityKind;
using P = TouchActivityPhase;
using G = TouchActivityGesture;
using C = ContextTouchControlKind;
using Q = TouchScriptQueryKind;

namespace {
struct Memory {
  std::unordered_map<uint32_t, uint8_t> bytes;
  size_t reads = 0;
  void Put(uint32_t address, uint32_t value, size_t width = 4) {
    for (size_t i = 0; i < width; ++i)
      bytes[address + static_cast<uint32_t>(i)] = static_cast<uint8_t>(value >> ((width - i - 1) * 8));
  }
  static bool Read(void* opaque, uint32_t address, void* out, size_t size) {
    auto& self = *static_cast<Memory*>(opaque); ++self.reads;
    for (size_t i = 0; i < size; ++i) {
      if (uint64_t(address) + i > UINT32_MAX) return false;
      auto f = self.bytes.find(address + static_cast<uint32_t>(i));
      if (f == self.bytes.end()) return false;
      static_cast<uint8_t*>(out)[i] = f->second;
    }
    return true;
  }
  TouchContextMemory View() { return {this, Read}; }
};
constexpr uint32_t kTable = 0x10000, kProgram = 0x20000, kThread = 0x30000, kLocals = 0x40000;
constexpr uint32_t kThread2 = 0x50000;
size_t Profile(K kind, uint32_t episode = 0) {
  for (size_t i = 0; i < kTouchActivityProfiles.size(); ++i)
    if (kTouchActivityProfiles[i].kind == kind && kTouchActivityProfiles[i].episode == episode) return i;
  assert(false); return 0;
}
void Prepare(Memory& m, size_t index, std::array<uint32_t,8> state, uint32_t thread = kThread, uint32_t serial = 71) {
  const auto& p = kTouchActivityProfiles[index];
  m.Put(0x82B39504, p.episode); m.Put(0x83192788, kTable); m.Put(0x8319278C, 1, 2);
  m.Put(0x8319277C, thread); m.Put(kTable, thread);
  m.Put(kProgram + 4, p.program_key); m.Put(kProgram + 16, p.code_size);
  // sub_82846780 stores the SCO local count at +20 and its flags at +22.
  m.Put(kProgram + 20, p.local_count, 2); m.Put(kProgram + 22, 0, 2);
  m.Put(thread + 4, serial); m.Put(thread + 8, p.program_key); m.Put(thread + 12, 1); m.Put(thread + 80, kLocals);
  for (size_t i = 0; i < p.locals.size(); ++i)
    if (p.locals[i] != UINT16_MAX) m.Put(kLocals + uint32_t(p.locals[i]) * 4, state[i]);
  PublishTouchActivityProgram(index, kProgram);
}
TouchContextSnapshot Admitted() {
  TouchContextSnapshot c;
  c.valid = c.playing = c.native_input_allowed = c.minigame_active = true;
  return c;
}
ContextTouchViewport View(float width = 1280, float height = 720) {
  ContextTouchViewport v;
  v.generation = 1; v.valid = v.focused = v.host_space = true;
  v.safe_width = v.output_width = v.logical_width = width;
  v.safe_height = v.output_height = v.logical_height = height;
  return v;
}
bool Has(const ContextTouchLayout& layout, Q kind, uint32_t action, uint32_t group = 0) {
  for (size_t i=0; i<layout.control_count; ++i) {
    const auto& c=layout.controls[i];
    if (c.visible && c.kind == C::kScriptButton && c.script.kind == kind &&
        c.script.action == action && c.script.input_group == group) return true;
  }
  return false;
}
void ProfilesAndMemory() {
  ResetTouchActivityPrograms();
  for (size_t i=0; i<kTouchActivityProfiles.size(); ++i) {
    const auto& p=kTouchActivityProfiles[i];
    const auto found=MatchTouchActivityProfile(p.name,p.code_size,p.sha256);
    assert(found && *found==i);
    assert(!MatchTouchActivityProfile(p.name,p.code_size,"invalid"));
    assert(!MatchTouchActivityProfile("unknown_activity",p.code_size,p.sha256));
    assert(!MatchTouchActivityProfile(p.name,0,p.sha256));
    PublishTouchActivityProgram(i,kProgram); assert(TouchActivityPrograms()[i]==kProgram);
    InvalidateTouchActivityProgram(p.name); assert(TouchActivityPrograms()[i]==0);
  }
  std::cout << "PASS every original fingerprint admits exact code and rejects changed hashes, sizes and names\n";
  const auto index=Profile(K::kBowling); Memory memory;
  Prepare(memory,index,{4,3}); auto context=Admitted();
  auto activity=ReadTouchActivityFacts(memory.View(),context);
  assert(activity.valid && activity.kind==K::kBowling && activity.phase==P::kStroke && activity.script_thread==71);
  assert(TouchActivityQueryMatches(memory.View(),activity));
  const auto& p=kTouchActivityProfiles[index];
  memory.Put(kLocals+uint32_t(p.locals[1])*4,18);
  assert(!TouchActivityQueryMatches(memory.View(),activity));
  assert(ReadTouchActivityFacts(memory.View(),context).phase==P::kAftertouch);
  memory.Put(kThread+4,72); assert(!TouchActivityQueryMatches(memory.View(),activity));
  memory.Put(kThread+12,2); assert(!ReadTouchActivityFacts(memory.View(),context).valid);
  Prepare(memory,index,{4,3}); memory.Put(kProgram+16,0); assert(!ReadTouchActivityFacts(memory.View(),context).valid);
  Prepare(memory,index,{4,3}); memory.Put(0x82B39504,2); assert(!ReadTouchActivityFacts(memory.View(),context).valid);
  Prepare(memory,index,{4,3}); memory.Put(kThread+80,UINT32_MAX); assert(!ReadTouchActivityFacts(memory.View(),context).valid);
  Prepare(memory,index,{4,3}); memory.Put(0x8319278C,UINT16_MAX,2); assert(!ReadTouchActivityFacts(memory.View(),context).valid);
  assert(memory.reads<10000);
  std::cout << "PASS guest reader rejects stale thread serials, changed phases, terminated scripts, corrupt pointers and episode mismatches\n";
  Prepare(memory,index,{4,3});
  auto unrestricted=context; unrestricted.minigame_active=false; unrestricted.gameplay_allowed=true;
  assert(!ReadTouchActivityFacts(memory.View(),unrestricted).valid);
  for (auto field : {&TouchContextSnapshot::loading,&TouchContextSnapshot::cutscene,&TouchContextSnapshot::frontend,&TouchContextSnapshot::map}) {
    auto blocked=context; blocked.*field=true; assert(!ReadTouchActivityFacts(memory.View(),blocked).valid);
  }
  Prepare(memory,index,{4,3},kThread2,72);
  memory.Put(kTable,kThread); memory.Put(kTable+4,kThread2); memory.Put(0x8319278C,2,2);
  assert(!ReadTouchActivityFacts(memory.View(),context).valid);
  Prepare(memory,index,{4,3}); const auto before_reset=ReadTouchActivityFacts(memory.View(),context);
  assert(before_reset.valid && TouchActivityQueryMatches(memory.View(),before_reset));
  ResetTouchActivityPrograms(); assert(!ReadTouchActivityFacts(memory.View(),context).valid);
  assert(!TouchActivityQueryMatches(memory.View(),before_reset));
  std::cout << "PASS ambient scripts, competing owners and world resets cannot create a specialized input owner\n";
  const auto dance=Profile(K::kDancing,2); Memory dancing;
  Prepare(dancing,dance,{0,99}); activity=ReadTouchActivityFacts(dancing.View(),context);
  assert(activity.valid && activity.phase==P::kPlaying);
  dancing.Put(kLocals+uint32_t(kTouchActivityProfiles[dance].locals[0])*4,1);
  assert(TouchActivityQueryMatches(dancing.View(),activity));
  dancing.Put(kLocals+uint32_t(kTouchActivityProfiles[dance].locals[0])*4,10);
  assert(!TouchActivityQueryMatches(dancing.View(),activity));
  ResetTouchActivityPrograms();
  std::cout << "PASS dance movement-to-hold retains a compatible contact but group prompts require a fresh owner\n";
}
void OriginalProgramHeaders() {
  for (size_t i = 0; i < kTouchActivityProfiles.size(); ++i) {
    ResetTouchActivityPrograms();
    const auto& p = kTouchActivityProfiles[i];
    std::array<uint32_t, 8> state{};
    switch (p.kind) {
      case K::kBowling: state = {4, 3}; break;
      case K::kPool: state = {3}; break;
      case K::kDarts: state = {1, 0}; break;
      case K::kQub3d: state = {2}; break;
      case K::kAirHockey: state = {4}; break;
      case K::kArmWrestling: state = {3, 4}; break;
      case K::kHiLo: state = {4, 5}; break;
      case K::kDancing: state = {0, 99}; break;
      case K::kChampagne: state = {1, 0}; break;
      case K::kGolf: state = {3, 0, 0}; break;
      case K::kCageFighting: state = {2}; break;
      case K::kTaxi: state = {1}; break;
      case K::kPoliceComputer: state = {2, 2}; break;
      default: assert(false);
    }
    Memory memory;
    Prepare(memory, i, state);
    auto context = Admitted();
    // Scripted activities can disable player control without setting the
    // global minigame flag or displaying a native help prompt.
    context.minigame_active = false;
    auto activity = ReadTouchActivityFacts(memory.View(), context);
    assert(activity.valid && activity.profile == i + 1);
    assert(TouchActivityQueryMatches(memory.View(), activity));
    memory.Put(kProgram + 22, UINT16_MAX, 2);
    assert(ReadTouchActivityFacts(memory.View(), context).valid);
    assert(TouchActivityQueryMatches(memory.View(), activity));
    memory.Put(kProgram + 20, 0, 2);
    memory.Put(kProgram + 22, p.local_count, 2);
    assert(!ReadTouchActivityFacts(memory.View(), context).valid);
    assert(!TouchActivityQueryMatches(memory.View(), activity));
  }
  ResetTouchActivityPrograms();
  std::cout << "PASS all episode activity profiles use the original loader header; flags cannot masquerade as the local count\n";
}
void Gestures() {
  TouchActivityGestureState g;
  g.Begin(G::kStrokeRight,0,0,0,0,kRadius,kBeginNs);
  g.Move(0,kTravel,kMoveNs); g.Move(0,-kTravel,kReverseNs); g.Release(kReverseNs);
  assert(g.Sample(kReverseNs,kFrame).axes[3]==kStrokeForward);
  assert(g.Sample(kReverseNs,kFrame).axes[3]==kStrokeBackward);
  assert(!g.Sample(kReverseNs,kFrame).nonzero());
  g.Begin(G::kStrokeRight,0,0,0,0,kRadius,kBeginNs); g.Move(0,kTravel,kMoveNs); g.Release(kMoveNs);
  assert(g.Sample(kMoveNs,kFrame).axes[3]>0); assert(!g.Sample(kMoveNs,kFrame).nonzero());
  g.Begin(G::kStrokeRight,0,0,0,0,kRadius,kBeginNs); g.Move(0,kTravel,kMoveNs); g.Cancel();
  assert(!g.Sample(kMoveNs,kFrame).nonzero());
  std::cout << "PASS sub-poll backswing and forward stroke survive release without inventing a forward shot on lift\n";
  for (auto gesture : {G::kRelativeLeft,G::kHorizontalLeft,G::kRelativeRight}) {
    g.Begin(gesture,0,0,0,0,kRadius,kBeginNs); g.Move(kTravel,kTravel,kMoveNs); g.Release(kMoveNs);
    assert(g.Sample(kMoveNs,kFrame).nonzero()); assert(!g.Sample(kMoveNs,kFrame).nonzero());
  }
  for (auto gesture : {G::kHoldLeft,G::kHoldRight,G::kCircleRight}) {
    g.Begin(gesture,0,0,0,0,kRadius,kBeginNs); g.Move(kTravel,kTravel,kMoveNs);
    const auto a=g.Sample(kMoveNs,kFrame), b=g.Sample(kMoveNs,kFrame);
    assert(a.nonzero() && a.axes==b.axes); g.Release(kMoveNs); assert(!g.Sample(kMoveNs,kFrame).nonzero());
  }
  std::cout << "PASS relative aim delivers once after release; stick holds and circular positions stop on release\n";
  for (auto gesture : {G::kAlternatingHorizontal,G::kAlternatingVertical}) {
    g.Begin(gesture,0,0,0,0,kRadius,kBeginNs);
    g.Move(1,1,kMoveNs); assert(!g.Sample(kMoveNs,kFrame).nonzero());
    g.Move(kTravel,kTravel,kReverseNs); assert(g.Sample(kReverseNs,kFrame).nonzero());
    assert(!g.Sample(kReverseNs,kFrame).nonzero());
    g.Move(-kTravel,-kTravel,kReverseNs); assert(g.Sample(kReverseNs,kFrame).nonzero());
    assert(!g.Sample(kReverseNs,kFrame).nonzero());
  }
  g.Begin(G::kQub3d,0,0,0,0,kRadius,kBeginNs); g.Release(kMoveNs);
  assert(g.Sample(kMoveNs,kFrame).raw_buttons==(uint32_t{1}<<11));
  assert(!g.Sample(kMoveNs,kFrame).nonzero());
  g.Begin(G::kQub3d,0,0,0,0,kRadius,kBeginNs); g.Move(-kTravel,0,kMoveNs); g.Move(kTravel,0,kReverseNs); g.Release(kReverseNs);
  assert(g.Sample(kReverseNs,kFrame).raw_buttons==(uint32_t{1}<<10));
  assert(!g.Sample(kReverseNs,kFrame).nonzero());
  assert(g.Sample(kReverseNs,kFrame).raw_buttons==(uint32_t{1}<<11));
  g.Begin(G::kQub3d,0,0,0,0,kRadius,kBeginNs); g.Move(0,kTravel,kMoveNs); g.Release(kMoveNs);
  assert(g.Sample(kMoveNs,kFrame).raw_buttons==(uint32_t{1}<<9));
  std::cout << "PASS alternating swipes reject jitter and do not auto-repeat; QUB3D taps, rotations and fast descent preserve discrete edges\n";
  for (int i=0;i<10000;++i) {
    g.Begin(G::kStrokeRight,0,0,0,0,kRadius,kBeginNs);
    g.Move(0,kTravel,kMoveNs); assert(!g.Sample(kExpiredNs,kFrame).nonzero());
    g.Move(std::numeric_limits<float>::infinity(),0,kReverseNs); assert(!g.Sample(kExpiredNs,kFrame).nonzero());
    g.Begin(G::kHoldRight,0,0,0,0,kRadius,kMoveNs); g.Move(kTravel,0,kBeginNs);
    assert(!g.Sample(kMoveNs,kFrame).nonzero());
  }
  g.Begin(G::kQub3d,0,0,0,0,kRadius,kBeginNs);
  for(int i=0;i<64;++i) g.Move(i%2?kTravel:-kTravel,0,kMoveNs);
  assert(g.released() && !g.Sample(kMoveNs,kFrame).nonzero());
  std::cout << "PASS stale gestures, invalid coordinates, reversed timestamps and bounded queue overflow fail closed across repeated sessions\n";
}
void Layouts() {
  struct Case { K kind; uint32_t episode; std::array<uint32_t,8> state; P phase; };
  const Case cases[] = {
    {K::kBowling,0,{4,15},P::kPosition},{K::kBowling,1,{4,3},P::kStroke},{K::kBowling,2,{4,18},P::kAftertouch},
    {K::kPool,0,{1,1},P::kPlaceBall},{K::kPool,1,{3,0},P::kAim},{K::kPool,2,{4,0},P::kStroke},
    {K::kDarts,0,{1,0},P::kAim},{K::kQub3d,0,{2,0},P::kPlaying},
    {K::kAirHockey,1,{4,0},P::kPlaying},{K::kArmWrestling,1,{3,4},P::kPlaying},
    {K::kHiLo,1,{4,5},P::kChoice},{K::kDancing,2,{0,99},P::kPlaying},
    {K::kDancing,2,{1,0},P::kHold},{K::kDancing,2,{10,0},P::kGroup},
    {K::kChampagne,2,{1,4,0,0,0,1,0,0},P::kShake},
    {K::kChampagne,2,{1,4,0,1,1,1,0,0},P::kSpray},
    {K::kChampagne,2,{1,4,1,1,0,1,0,0},P::kDrink},
    {K::kGolf,2,{3,0,0,0},P::kSwing},{K::kGolf,2,{3,0,1,0},P::kSwing},
    {K::kTaxi,0,{1,0},P::kChoice},{K::kTaxi,2,{2,0},P::kTravel},
    {K::kPoliceComputer,0,{2,2},P::kChoice},{K::kCageFighting,2,{2,0},P::kMenu},
  };
  for (const auto& c:cases) {
    auto a=ClassifyTouchActivity(Profile(c.kind,c.episode),c.state,true,false);
    assert(a.valid && a.phase==c.phase); a.script_thread=71;
    for (bool handed : {false,true}) for (auto dimensions : {std::pair{1280.0f,720.0f},std::pair{720.0f,1280.0f},std::pair{640.0f,360.0f}})
    for (float scale : {0.65f, 1.0f, 1.8f}) {
      ContextTouchLayoutOptions options; options.activity=a; options.context_generation=9; options.contextual=true; options.left_handed=handed;
      options.button_scale=scale;
      const auto v=View(dimensions.first,dimensions.second);
      const auto layout=BuildContextTouchLayout(ContextTouchMode::kMinigame,v,{},options);
      assert(layout.activity.SameSession(a) && layout.control_count>0 && layout.control_count<=layout.controls.size());
      bool pause=false;
      for(size_t i=0;i<layout.control_count;++i) {
        const auto& control=layout.controls[i];
        assert(control.action!=TouchAction::kFire && control.action!=TouchAction::kWeaponWheel && control.kind!=C::kLookSurface);
        assert(control.center_x>=v.safe_x && control.center_x<=v.safe_width && control.center_y>=v.safe_y && control.center_y<=v.safe_height);
        pause|=control.action==TouchAction::kPause;
        if(control.kind==C::kScriptButton || control.kind==C::kActivitySurface) {
          assert(control.script.script_thread==71 && control.script.generation==9);
          assert(control.label.back()==0 && control.accessible_name.back()==0);
        }
        if (control.visible && control.kind != C::kActivitySurface) {
          assert(control.center_x-control.radius>=v.safe_x && control.center_x+control.radius<=v.safe_width);
          assert(control.center_y-control.radius>=v.safe_y && control.center_y+control.radius<=v.safe_height);
          for (size_t j=0; j<i; ++j) {
            const auto& other=layout.controls[j];
            if (!other.visible || other.kind==C::kActivitySurface) continue;
            assert(std::hypot(control.center_x-other.center_x,control.center_y-other.center_y)>=control.radius+other.radius);
          }
        }
      }
      assert(pause);
      if (c.kind!=K::kTaxi && c.kind!=K::kPoliceComputer)
        assert(Has(layout,Q::kControlHeld,23,2));
      assert(BuildContextTouchLayout(ContextTouchMode::kDisabled,v,{},options).control_count==0);
      if(c.kind==K::kHiLo) assert(Has(layout,Q::kControlHeld,78,2) && Has(layout,Q::kControlHeld,79,2));
      if(c.kind==K::kDarts) assert(Has(layout,Q::kRawButton,16) && Has(layout,Q::kRawButton,7) && Has(layout,Q::kRawButton,6));
      if(c.kind==K::kGolf && !a.analog_golf) assert(Has(layout,Q::kControlHeld,1,0));
      if(c.kind==K::kDancing && c.phase==P::kGroup)
        for(auto button:{14u,15u,16u,17u}) assert(Has(layout,Q::kRawButton,button));
      if(c.kind==K::kQub3d)
        for(auto button:{8u,9u,10u,11u,14u,15u,16u,17u}) assert(Has(layout,Q::kRawButton,button));
    }
  }
  std::cout << "PASS activity phase layouts keep separate in-bounds buttons, original controls, Leave and Pause across sizes, scales and handedness\n";
}
}
int main() { OriginalProgramHeaders(); ProfilesAndMemory(); Gestures(); Layouts(); }
