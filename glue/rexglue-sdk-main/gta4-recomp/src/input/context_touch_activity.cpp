#include "input/context_touch_activity.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <numbers>

namespace gta4::input {
namespace {
std::mutex program_mutex;
std::array<uint32_t, kTouchActivityProfiles.size()> programs{};
using K = TouchActivityKind;
using P = TouchActivityPhase;
using G = TouchActivityGesture;

int32_t Axis(double value) noexcept {
  if (!std::isfinite(value)) return 0;
  return static_cast<int32_t>(std::lround(std::clamp(value, -128.0, 127.0)));
}
int Sign(double value) noexcept { return (value > 0.0) - (value < 0.0); }
}  // namespace

std::optional<size_t> MatchTouchActivityProfile(std::string_view name, uint32_t size,
                                               std::string_view digest) noexcept {
  for (size_t i = 0; i < kTouchActivityProfiles.size(); ++i) {
    const auto& p = kTouchActivityProfiles[i];
    if (p.name == name && p.code_size == size && p.sha256 == digest) return i;
  }
  return std::nullopt;
}
void PublishTouchActivityProgram(size_t profile, uint32_t address) noexcept {
  std::lock_guard lock(program_mutex);
  if (profile >= programs.size()) return;
  const auto& matched = kTouchActivityProfiles[profile];
  for (size_t i = 0; i < programs.size(); ++i) {
    const auto& p = kTouchActivityProfiles[i];
    if (p.name == matched.name && p.sha256 == matched.sha256 && p.code_size == matched.code_size)
      programs[i] = address;
  }
}
void InvalidateTouchActivityProgram(std::string_view name) noexcept {
  std::lock_guard lock(program_mutex);
  for (size_t i = 0; i < programs.size(); ++i)
    if (kTouchActivityProfiles[i].name == name) programs[i] = 0;
}
void ResetTouchActivityPrograms() noexcept {
  std::lock_guard lock(program_mutex);
  programs.fill(0);
}
std::array<uint32_t, kTouchActivityProfiles.size()> TouchActivityPrograms() noexcept {
  std::lock_guard lock(program_mutex);
  return programs;
}

TouchActivitySnapshot ClassifyTouchActivity(size_t index,
    const std::array<uint32_t, 8>& s, bool minigame, bool gameplay) noexcept {
  if (index >= kTouchActivityProfiles.size()) return {};
  const auto& profile = kTouchActivityProfiles[index];
  TouchActivitySnapshot out{.kind = profile.kind, .program_key = profile.program_key,
      .episode = profile.episode, .profile = static_cast<uint32_t>(index + 1), .state = s};
  // A resident background script never acquires touch ownership on its own.
  if (!minigame && gameplay) return {};
  out.phase = P::kWaiting;
  switch (out.kind) {
    case K::kBowling:
      if (s[0] != 4) return {};
      switch (s[1]) {
        case 15: out.phase = P::kPosition; break;
        case 3: out.phase = P::kStroke; break;
        case 18: out.phase = P::kAftertouch; break;
        case 32: case 31: case 34: case 30: out.phase = P::kSetup; break;
        case 27: case 29: case 28: out.phase = P::kQuit; break;
        case 22: case 23: out.phase = P::kResult; break;
        default: break;
      }
      break;
    case K::kPool:
      if (s[0] > 8) return {};
      switch (s[0]) {
        case 0: out.phase = P::kSetup; break;
        case 1: out.phase = P::kPlaceBall; break;
        case 3: out.phase = P::kAim; break;
        case 4: out.phase = P::kStroke; break;
        case 7: out.phase = P::kResult; break;
        case 8: out.phase = P::kQuit; break;
        default: break;
      }
      break;
    case K::kDarts:
      if (s[0] > 2) return {};
      if (s[0] == 0) out.phase = P::kSetup;
      else if (s[0] == 2) out.phase = P::kResult;
      else if (s[1] == 0) out.phase = P::kAim;
      else if (s[1] == 3) out.phase = P::kResult;
      break;
    case K::kQub3d:
      if (s[0] > 6) return {};
      out.phase = s[0] == 2 ? P::kPlaying : s[0] == 3 ? P::kResult : P::kMenu;
      break;
    case K::kAirHockey:
      if (s[0] < 2 || s[0] > 8) return {};
      out.phase = s[0] == 4 ? P::kPlaying : s[0] == 7 ? P::kResult :
                  s[0] == 8 ? P::kQuit : s[0] == 2 || s[0] == 3 ? P::kSetup : P::kWaiting;
      break;
    case K::kArmWrestling:
      if (s[0] != 3) return {};
      out.phase = s[1] == 4 ? P::kPlaying : s[1] == 2 || s[1] == 3 ? P::kSetup :
                  s[1] == 10 ? P::kResult : s[1] == 5 ? P::kQuit : P::kWaiting;
      break;
    case K::kHiLo:
      if (s[0] != 4) return {};
      out.phase = s[1] == 5 ? P::kChoice : s[1] == 0 ? P::kSetup :
                  s[1] == 17 || s[1] == 12 || s[1] == 13 ? P::kResult :
                  s[1] == 18 ? P::kQuit : P::kWaiting;
      break;
    case K::kDancing:
      if (s[0] > 10) return {};
      out.phase = s[0] == 0 && s[1] == 99 ? P::kPlaying : s[0] == 1 ? P::kHold :
                  s[0] == 10 ? P::kGroup : s[0] == 0 ? P::kSetup : P::kWaiting;
      break;
    case K::kChampagne:
      if (s[0] != 1) return {};
      if (s[1] == 0) { out.kind = K::kDrinking; out.phase = P::kDrink; }
      else if (s[1] == 4 && s[5] && !s[6]) {
        // drinking.sco: local443 admits drinking, local603 ends shaking;
        // local498 admits spray. These are booleans, not animation timings.
        out.phase = s[2] ? P::kDrink : !s[3] ? P::kShake : s[4] ? P::kSpray : P::kWaiting;
      } else out.phase = P::kWaiting;
      break;
    case K::kGolf:
      if (s[0] < 2 || s[0] > 7) return {};
      // PRINT_HELP dispatcher at 0x5895 chooses argument 1 (the B/analog
      // instructions) for local2893 == 0, argument 0 for button controls.
      out.analog_golf = s[2] == 0;
      out.phase = s[0] == 3 ? P::kSwing : s[0] == 2 ? P::kSetup :
                  s[0] == 6 ? P::kResult : s[0] == 7 ? P::kQuit : P::kWaiting;
      break;
    case K::kCageFighting:
      if (s[0] < 2 || s[0] > 8) return {};
      out.native_combat = gameplay;
      out.phase = gameplay ? P::kMelee : P::kMenu;
      break;
    case K::kTaxi:
      if (s[0] != 1 && s[0] != 2) return {};
      out.phase = s[0] == 1 ? P::kChoice : P::kTravel;
      break;
    case K::kPoliceComputer:
      if (s[0] > 3 || gameplay) return {};
      out.phase = s[0] == 2 && s[1] == 2 ? P::kChoice : P::kMenu;
      break;
    default: return {};
  }
  out.valid = true;
  return out;
}

std::string_view TouchActivityName(K kind) noexcept {
  constexpr std::array names{"", "BOWLING", "POOL", "DARTS", "QUB3D", "AIR HOCKEY",
      "ARM WRESTLING", "HI-LO", "DANCING", "CHAMPAGNE", "GOLF", "CAGE FIGHTING",
      "DRINKING", "TAXI", "POLICE COMPUTER"};
  const auto i = static_cast<size_t>(kind);
  return i < names.size() ? names[i] : "";
}
std::string_view TouchActivityPhaseName(P phase) noexcept {
  constexpr std::array names{"", "READY", "POSITION", "AIM", "PLACE CUE BALL", "STROKE",
      "ADJUST ROLL", "PLAY", "CHOOSE", "HOLD", "GROUP DANCE", "SHAKE", "SPRAY", "DRINK",
      "SWING", "FIGHT", "MENU", "TRAVELLING", "RESULT", "LEAVE?", "WAIT"};
  const auto i = static_cast<size_t>(phase);
  return i < names.size() ? names[i] : "";
}

bool TouchActivityGestureOutput::nonzero() const noexcept {
  return raw_buttons || std::any_of(axes.begin(), axes.end(), [](int32_t a) { return a != 0; });
}
void TouchActivityGestureState::Begin(G gesture, float x, float y, float cx, float cy,
                                      float radius, uint64_t timestamp) noexcept {
  Cancel();
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(cx) || !std::isfinite(cy) ||
      !std::isfinite(radius) || radius <= 0) return;
  gesture_ = gesture;
  x_ = origin_x_ = x; y_ = origin_y_ = y;
  center_x_ = cx; center_y_ = cy; radius_ = radius; last_ns_ = last_motion_ns_ = timestamp;
  released_ = false;
}
void TouchActivityGestureState::Cancel() noexcept { *this = {}; released_ = true; }
void TouchActivityGestureState::Push(int32_t x, int32_t y, uint64_t timestamp) noexcept {
  if (!x && !y) return;
  if (count_ && gesture_ != G::kQub3d) {
    auto& last = samples_[(head_ + count_ - 1) % kCapacity];
    if (Sign(last.x) == Sign(x) && Sign(last.y) == Sign(y)) {
      last = {x, y, timestamp};
      return;
    }
  }
  if (count_ == kCapacity) { Cancel(); return; }
  samples_[(head_ + count_) % kCapacity] = {x, y, timestamp};
  ++count_;
}
void TouchActivityGestureState::Move(float x, float y, uint64_t timestamp) noexcept {
  if (released_ || gesture_ == G::kNone) return;
  if (!std::isfinite(x) || !std::isfinite(y) || timestamp < last_ns_) { Cancel(); return; }
  const double dx = double(x) - x_, dy = double(y) - y_;
  const double seconds = double(timestamp - last_ns_) / 1000000000.0;
  x_ = x; y_ = y; last_ns_ = timestamp;
  if (timestamp - last_motion_ns_ > kMaximumSampleAgeNanoseconds) {
    dx_ = dy_ = travel_ = 0.0;
  }
  if (dx != 0.0 || dy != 0.0) last_motion_ns_ = timestamp;
  dx_ += dx; dy_ += dy;
  moved_ |= std::hypot(double(x) - origin_x_, double(y) - origin_y_) > radius_ * 0.08;
  if (gesture_ == G::kStrokeRight && seconds > 0.0 && (dx != 0.0 || dy != 0.0)) {
    const double speed = 128.0 / (radius_ * 8.0 * seconds);
    Push(Axis(dx_ * speed), Axis(dy_ * speed), timestamp);
    dx_ = dy_ = 0.0;
  } else if (gesture_ == G::kAlternatingHorizontal || gesture_ == G::kAlternatingVertical) {
    const double movement = gesture_ == G::kAlternatingHorizontal ? dx : dy;
    if (Sign(movement) != Sign(travel_)) travel_ = 0.0;
    travel_ += movement;
    if (std::abs(travel_) >= radius_ * 0.12) {
      const int32_t extent = travel_ < 0 ? -128 : 127;
      Push(gesture_ == G::kAlternatingHorizontal ? extent : 0,
           gesture_ == G::kAlternatingVertical ? extent : 0, timestamp);
      travel_ = 0.0;
    }
  } else if (gesture_ == G::kQub3d) {
    if (std::abs(dx_) >= radius_ * 0.24 && std::abs(dx_) > std::abs(dy_)) {
      Push(dx_ < 0 ? -1 : 1, 0, timestamp);
      dx_ = dy_ = 0.0;
      moved_ = true;
    } else if (dy_ >= radius_ * 0.24) {
      if (!drop_) Push(0, 1, timestamp);
      drop_ = true; moved_ = true; dx_ = dy_ = 0.0;
    }
  }
}
void TouchActivityGestureState::Release(uint64_t timestamp) noexcept {
  if (timestamp < last_ns_) { Cancel(); return; }
  if (gesture_ == G::kQub3d && !moved_ && !released_) Push(1, 0, timestamp);
  released_ = true; drop_ = false;
  // Releasing never synthesizes a forward cue or bowling stroke.
}
TouchActivityGestureOutput TouchActivityGestureState::Sample(uint64_t now, double frame) noexcept {
  TouchActivityGestureOutput out;
  if (gesture_ == G::kNone || !std::isfinite(frame) || frame <= 0.0) return out;
  now = std::max(now, last_ns_);
  // Bound replay latency. Long pauses never deliver old physical gestures.
  for (; count_ && now - samples_[head_].timestamp_ns > kMaximumSampleAgeNanoseconds; --count_)
    head_ = (head_ + 1) % kCapacity;
  if (gesture_ == G::kQub3d) {
    if (!released_ && drop_) out.raw_buttons |= uint32_t{1} << 9;
    if (button_gap_) { button_gap_ = false; return out; }
    if (count_) {
      const auto sample = samples_[head_]; head_ = (head_ + 1) % kCapacity; --count_;
      out.raw_buttons |= uint32_t{1} << (sample.y > 0 ? 9 : sample.x < 0 ? 10 : 11);
      button_gap_ = true;
    }
    return out;
  }
  if (gesture_ == G::kStrokeRight || gesture_ == G::kAlternatingHorizontal ||
      gesture_ == G::kAlternatingVertical) {
    if (count_) {
      const auto sample = samples_[head_]; head_ = (head_ + 1) % kCapacity; --count_;
      out.axes[2] = sample.x; out.axes[3] = sample.y;
    }
    return out;
  }
  if (released_ && (gesture_ == G::kCircleRight || gesture_ == G::kHoldLeft || gesture_ == G::kHoldRight)) return out;
  if (gesture_ == G::kCircleRight) {
    const double x = (double(x_) - center_x_) / radius_;
    const double y = (double(y_) - center_y_) / radius_;
    const double length = std::hypot(x, y);
    if (length < 0.20) return out;
    out.axes[2] = Axis(x / std::max(1.0, length) * 128.0);
    out.axes[3] = Axis(y / std::max(1.0, length) * 128.0);
  } else if (gesture_ == G::kHoldLeft || gesture_ == G::kHoldRight) {
    const size_t axis = gesture_ == G::kHoldLeft ? 0 : 2;
    const double x = (double(x_) - origin_x_) / radius_;
    const double y = (double(y_) - origin_y_) / radius_;
    const double length = std::hypot(x, y);
    out.axes[axis] = Axis(x / std::max(1.0, length) * 128.0);
    out.axes[axis + 1] = Axis(y / std::max(1.0, length) * 128.0);
  } else {
    // A stationary Up must not revive an old unconsumed aiming delta. Holds
    // above intentionally remain valid without motion; relative input does not.
    if (now - last_motion_ns_ > kMaximumSampleAgeNanoseconds) {
      dx_ = dy_ = 0.0;
      return out;
    }
    const size_t axis = gesture_ == G::kRelativeRight ? 2 : 0;
    const double scale = 128.0 / (radius_ * 8.0 * frame);
    out.axes[axis] = Axis(dx_ * scale);
    out.axes[axis + 1] = gesture_ == G::kHorizontalLeft ? 0 : Axis(dy_ * scale);
    dx_ = dy_ = 0.0;
  }
  return out;
}

std::optional<uint8_t> TouchWeaponWheelSelection(const ContextTouchLayout& layout,
                                                float x, float y) noexcept {
  const auto& v = layout.viewport;
  if (!v.valid || !std::isfinite(x) || !std::isfinite(y) ||
      x < v.safe_x || y < v.safe_y || x > v.safe_x + v.safe_width || y > v.safe_y + v.safe_height)
    return std::nullopt;
  const double cx = v.safe_x + v.safe_width * 0.5, cy = v.safe_y + v.safe_height * 0.5;
  const double dx = x - cx, dy = y - cy, distance = std::hypot(dx, dy);
  const double ring = std::min(v.safe_width, v.safe_height) * 0.34;
  if (distance < ring * 0.35 || distance > ring * 1.7 || ring <= 0) return std::nullopt;
  const double boundary = std::cos(std::numbers::pi / 11.0);
  for (size_t i = 0; i < layout.control_count; ++i) {
    const auto& c = layout.controls[i];
    if (!c.visible || c.action != TouchAction::kWeaponSelect || c.weapon_slot >= 11) continue;
    const double vx = c.center_x - cx, vy = c.center_y - cy, r = std::hypot(vx, vy);
    if (r > 0 && (dx * vx + dy * vy) / (distance * r) >= boundary) return c.weapon_slot;
  }
  return std::nullopt;
}
}  // namespace gta4::input
