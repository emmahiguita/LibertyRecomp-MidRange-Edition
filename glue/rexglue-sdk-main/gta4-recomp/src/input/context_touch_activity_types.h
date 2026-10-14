#pragma once

#include <array>
#include <cstdint>

namespace gta4::input {

// Stable host identities, never native task IDs or guessed script addresses.
enum class TouchActivityKind : uint8_t {
  kNone, kBowling, kPool, kDarts, kQub3d, kAirHockey, kArmWrestling,
  kHiLo, kDancing, kChampagne, kGolf, kCageFighting, kDrinking, kTaxi,
  kPoliceComputer,
};
enum class TouchActivityPhase : uint8_t {
  kNone, kSetup, kPosition, kAim, kPlaceBall, kStroke, kAftertouch,
  kPlaying, kChoice, kHold, kGroup, kShake, kSpray, kDrink, kSwing,
  kMelee, kMenu, kTravel, kResult, kQuit, kWaiting,
};
enum class TouchActivityGesture : uint8_t {
  kNone, kRelativeLeft, kRelativeRight, kHorizontalLeft, kStrokeRight,
  kAlternatingHorizontal, kAlternatingVertical, kCircleRight,
  kHoldLeft, kHoldRight, kQub3d,
};

struct TouchActivitySnapshot {
  TouchActivityKind kind = TouchActivityKind::kNone;
  TouchActivityPhase phase = TouchActivityPhase::kNone;
  uint32_t script_thread = 0;  // Monotonic retail serial, not allocation address.
  uint32_t program_key = 0;
  uint32_t episode = 0;
  uint32_t profile = 0;
  std::array<uint32_t, 8> state{};
  bool valid = false;
  bool native_combat = false;
  bool analog_golf = false;

  bool SameSession(const TouchActivitySnapshot& other) const noexcept {
    return valid == other.valid && kind == other.kind && script_thread == other.script_thread &&
           program_key == other.program_key && episode == other.episode && profile == other.profile;
  }
};

// Only the dance movement/hold transition preserves a deflected finger. Every
// other changed phase cancels contacts; a previous A/Fire must not become Confirm.
inline bool CompatibleActivityContacts(const TouchActivitySnapshot& a,
                                       const TouchActivitySnapshot& b) noexcept {
  if (!a.SameSession(b) || a.analog_golf != b.analog_golf) return false;
  if (a.phase == b.phase) return true;
  const auto dance_move = [](TouchActivityPhase p) {
    return p == TouchActivityPhase::kPlaying || p == TouchActivityPhase::kHold;
  };
  return a.kind == TouchActivityKind::kDancing && dance_move(a.phase) && dance_move(b.phase);
}

}  // namespace gta4::input
