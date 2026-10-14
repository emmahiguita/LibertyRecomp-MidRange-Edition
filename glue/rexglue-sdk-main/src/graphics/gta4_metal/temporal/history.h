#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace rex::graphics::gta4_metal::temporal {
enum class Method : uint32_t { kTaa, kMetalFx };
enum class InputCoverage : uint32_t {
  kUnknown, kCameraOnly, kSceneMotion,
  // Every missing/invalid correspondence is marked reactive. AA can reject
  // those samples; frame interpolation has no equivalent validity input.
  kReactiveSceneMotion
};
enum class ColorDomain : uint32_t { kUnknown, kSceneLinear, kDisplayLinear, kDisplayPerceptual };
struct Configuration {
  uint32_t input_width = 0, input_height = 0, output_width = 0, output_height = 0;
  Method method = Method::kTaa;
  bool depth_reversed = false;
  bool operator==(const Configuration&) const = default;
};
struct Frame {
  uint64_t sequence = 0, epoch = 0, view = 0, time_ns = 0;
  std::array<float, 2> jitter{};  // Actual scene displacement in input pixels.
  float near_plane = 0.1f, far_plane = 1000.0f, field_of_view = 60.0f, aspect = 1.0f;
  float pre_exposure = 1.0f, exposure = 1.0f;
  InputCoverage coverage = InputCoverage::kUnknown;
  ColorDomain color_domain = ColorDomain::kUnknown;
  bool scene_without_ui = false, jitter_applied = false, camera_cut = false;
};
struct HistoryDecision {
  bool accepted = false, reset = true;
  float delta_seconds = 0;
  std::string_view reason;
};
inline bool ValidConfiguration(const Configuration& c) {
  return c.input_width && c.input_height && c.output_width && c.output_height &&
         c.input_width <= 16384 && c.input_height <= 16384 && c.output_width <= 16384 &&
         c.output_height <= 16384 && c.input_width <= c.output_width &&
         c.input_height <= c.output_height &&
         (c.method == Method::kMetalFx ||
          (c.method == Method::kTaa && c.input_width == c.output_width &&
           c.input_height == c.output_height));
}
// Evaluate without mutating history. Commit only after successful encoding.
// Repeated images and stale view streams are not new temporal samples.
class History {
 public:
  HistoryDecision Inspect(const Frame& f, bool allow_reactive = false) const {
    if (!f.sequence || !f.epoch || !f.view || !f.time_ns)
      return {false, true, 0, "missing temporal frame identity"};
    if (f.coverage != InputCoverage::kSceneMotion &&
        !(allow_reactive && f.coverage == InputCoverage::kReactiveSceneMotion))
      return {false, true, 0, "complete scene motion is unavailable"};
    if (f.color_domain == ColorDomain::kUnknown || !f.scene_without_ui || !f.jitter_applied)
      return {false, true, 0, "temporal color/jitter/UI contract is incomplete"};
    if (!std::isfinite(f.near_plane) || !std::isfinite(f.far_plane) || f.near_plane <= 0 ||
        f.far_plane <= f.near_plane || !std::isfinite(f.field_of_view) || f.field_of_view <= 0 ||
        f.field_of_view >= 180 || !std::isfinite(f.aspect) || f.aspect <= 0 ||
        !std::isfinite(f.pre_exposure) || f.pre_exposure <= 0 || !std::isfinite(f.exposure) ||
        f.exposure <= 0)
      return {false, true, 0, "invalid temporal camera or exposure"};
    for (float value : f.jitter)
      if (!std::isfinite(value) || std::abs(value) > 0.5f)
        return {false, true, 0, "invalid temporal jitter"};
    if (!valid_) return {true, true, 0, "first frame"};
    if (f.epoch < previous_.epoch) return {false, true, 0, "stale temporal epoch"};
    if (f.epoch != previous_.epoch || f.view != previous_.view)
      return {true, true, 0, "new view or epoch"};
    if (f.sequence <= previous_.sequence || f.time_ns <= previous_.time_ns)
      return {false, true, 0, "duplicate or out-of-order temporal frame"};
    const float seconds = float(double(f.time_ns - previous_.time_ns) * 1.0e-9);
    if (f.camera_cut || f.color_domain != previous_.color_domain ||
        previous_.sequence == UINT64_MAX || f.sequence != previous_.sequence + 1 ||
        seconds > 0.25f || f.near_plane != previous_.near_plane ||
        f.far_plane != previous_.far_plane ||
        std::abs(f.field_of_view - previous_.field_of_view) > 0.01f ||
        std::abs(f.aspect - previous_.aspect) > 0.0001f)
      return {true, true, seconds, "history discontinuity"};
    return {true, false, seconds, {}};
  }
  void Commit(const Frame& f) {
    previous_ = f;
    valid_ = true;
  }
  void Reset() {
    previous_ = {};
    valid_ = false;
  }
  const Frame& previous() const { return previous_; }

 private:
  Frame previous_{};
  bool valid_ = false;
};
inline std::array<float, 2> Jitter(uint64_t sequence) {
  const uint32_t index = uint32_t(sequence % 32) + 1;
  auto halton = [](uint32_t n, uint32_t radix) {
    float value = 0, weight = 1;
    for (; n; n /= radix) {
      weight /= float(radix);
      value += weight * float(n % radix);
    }
    return value - 0.5f;
  };
  return {halton(index, 2), halton(index, 3)};
}
// One midpoint followed by its real frame. Never acknowledge a game
// publication for the midpoint. An epoch change invalidates a pending pair.
class PairDelivery {
 public:
  enum class Kind { kNone, kGenerated, kReal };
  bool Begin(uint64_t epoch, uint64_t publication, bool generated) {
    if (!epoch || !publication || epoch < epoch_ ||
        (epoch == epoch_ && publication <= last_publication_))
      return false;
    if (epoch != epoch_) Reset();
    if (kind_ != Kind::kNone) return false;
    epoch_ = epoch;
    publication_ = publication;
    kind_ = generated ? Kind::kGenerated : Kind::kReal;
    return true;
  }
  bool Consume() {
    if (kind_ == Kind::kGenerated) {
      kind_ = Kind::kReal;
      return false;
    }
    if (kind_ == Kind::kReal) {
      last_publication_ = publication_;
      kind_ = Kind::kNone;
      return true;
    }
    return false;
  }
  Kind kind() const { return kind_; }
  uint64_t publication() const { return publication_; }
  void Reset() {
    epoch_ = publication_ = last_publication_ = 0;
    kind_ = Kind::kNone;
  }

 private:
  uint64_t epoch_ = 0, publication_ = 0, last_publication_ = 0;
  Kind kind_ = Kind::kNone;
};
}  // namespace rex::graphics::gta4_metal::temporal
