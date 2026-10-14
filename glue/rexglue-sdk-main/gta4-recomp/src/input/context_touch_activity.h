#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "input/context_touch_activity_profiles.h"
#include "input/context_touch_layout.h"

namespace gta4::input {

struct TouchContextMemory;
struct TouchContextSnapshot;

// Loader admission occurs before native relocation and publication is checked
// after the original program constructor returns. Unknown code stays generic.
std::optional<size_t> MatchTouchActivityProfile(std::string_view name,
                                               uint32_t code_size,
                                               std::string_view sha256) noexcept;
void PublishTouchActivityProgram(size_t profile, uint32_t program_address) noexcept;
void InvalidateTouchActivityProgram(std::string_view name) noexcept;
void ResetTouchActivityPrograms() noexcept;
std::array<uint32_t, kTouchActivityProfiles.size()> TouchActivityPrograms() noexcept;

TouchActivitySnapshot ClassifyTouchActivity(size_t profile,
    const std::array<uint32_t, 8>& state, bool minigame, bool gameplay_allowed) noexcept;
TouchActivitySnapshot ReadTouchActivityFacts(const TouchContextMemory& memory,
                                             const TouchContextSnapshot& context) noexcept;
bool TouchActivityQueryMatches(const TouchContextMemory& memory,
                               const TouchActivitySnapshot& expected) noexcept;

std::string_view TouchActivityName(TouchActivityKind kind) noexcept;
std::string_view TouchActivityPhaseName(TouchActivityPhase phase) noexcept;
ContextTouchLayout BuildTouchActivityLayout(const ContextTouchViewport& viewport,
    const TouchActivitySnapshot& activity, uint64_t generation,
    std::span<const TouchScriptControl> observed, const ContextTouchLayoutOptions& options);

// Gesture samples use the retail GET_POSITION_OF_ANALOGUE_STICKS convention:
// X right positive, Y down positive, retail range [-128,127]. This is not an
// XInput int16 vector and must not pass through the native-pad replay as well.
struct TouchActivityGestureOutput {
  std::array<int32_t, 4> axes{};
  uint32_t raw_buttons = 0;
  bool nonzero() const noexcept;
};

class TouchActivityGestureState {
 public:
  void Begin(TouchActivityGesture gesture, float x, float y, float center_x,
             float center_y, float radius, uint64_t timestamp_ns) noexcept;
  void Move(float x, float y, uint64_t timestamp_ns) noexcept;
  void Release(uint64_t timestamp_ns) noexcept;
  void Cancel() noexcept;
  TouchActivityGestureOutput Sample(uint64_t timestamp_ns, double frame_seconds) noexcept;
  bool pending() const noexcept { return count_ != 0 || pending_button_ != 0; }
  bool released() const noexcept { return released_; }

 private:
  struct SamplePoint { int32_t x = 0, y = 0; uint64_t timestamp_ns = 0; };
  void Push(int32_t x, int32_t y, uint64_t timestamp_ns) noexcept;
  static constexpr size_t kCapacity = 16;
  static constexpr uint64_t kMaximumSampleAgeNanoseconds = 250000000;
  std::array<SamplePoint, kCapacity> samples_{};
  size_t head_ = 0, count_ = 0;
  TouchActivityGesture gesture_ = TouchActivityGesture::kNone;
  float x_ = 0, y_ = 0, origin_x_ = 0, origin_y_ = 0;
  float center_x_ = 0, center_y_ = 0, radius_ = 1;
  double dx_ = 0, dy_ = 0, travel_ = 0;
  uint64_t last_ns_ = 0;
  uint64_t last_motion_ns_ = 0;
  uint32_t pending_button_ = 0;
  bool released_ = false, moved_ = false, drop_ = false, button_gap_ = false;
};

// Angle-based wheel regions tolerate finger drift; returning no slot means
// cancellation. Unowned slots are never selected and the center remains inert.
std::optional<uint8_t> TouchWeaponWheelSelection(const ContextTouchLayout& layout,
                                                float x, float y) noexcept;

}  // namespace gta4::input
