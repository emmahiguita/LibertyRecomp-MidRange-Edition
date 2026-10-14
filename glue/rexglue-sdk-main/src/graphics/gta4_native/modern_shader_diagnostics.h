#pragma once
#include "modern_shader_policy.h"
#include "split_postfx_parameters.h"
#include "sun_shafts_parameters.h"
#include "modern_diagnostic_gate.h"
#include <array>
#include <string_view>

namespace rex::graphics::gta4_native {
enum class ModernEffect : uint8_t {
  kDof, kFog, kBloom, kTone, kSun, kCloudMask, kSky, kMotion, kGrain, kWater, kLight, kCount
};
enum class ModernExecution : uint8_t { kEncoded, kSkipped, kFallback, kStock, kReused, kCount };
struct ModernTextureTrace {
  uint32_t handle = 0, width = 0, height = 0;
  uint64_t generation = 0;
  bool ready = false;
};
struct ModernDrawTrace {
  uint64_t vertex = 0, pixel = 0;
  uint32_t phase = 0, samples = 0, width = 0, height = 0;
  bool vertex_override = false, pixel_override = false, tone_lut = false, dof_bypass = false;
  const EnvironmentalDataV2* environment = nullptr;
};
struct ModernDofTrace {
  uint64_t pixel = 0;
  uint32_t phase = 0;
  ModernTextureTrace scene, half_scene, depth, mask;
  SplitPostFxParameters parameters{};
  bool encoded = false;
  std::string_view reason;
};
// Render-owner state only. No GPU readbacks or synchronization; "encoded" means
// commands were recorded successfully, not that every pixel received an effect.
class ModernShaderDiagnostics {
 public:
  void BeginFrame(bool enabled, ModernShaderSettings settings, const char* backend);
  bool enabled() const { return enabled_; }
  uint64_t frame() const { return frame_; }
  bool Failure(std::string_view point, uint64_t command, uint32_t type, uint32_t phase,
               uint64_t vertex, uint64_t pixel, std::string_view reason);
  bool Sky(uint64_t vertex, uint64_t pixel, uint32_t samples, uint64_t variant, uint32_t target);
  void SkySkipped(uint64_t command, uint64_t pixel, uint32_t phase, std::string_view reason);
  bool Observe(ModernEffect effect, ModernExecution result);
  uint64_t Count(ModernEffect effect, ModernExecution result) const {
    return counts_[size_t(effect)][size_t(result)];
  }
  void Draw(const ModernDrawTrace& draw);
  void Dof(const ModernDofTrace& trace);
  void SkipPostFx(uint64_t pixel, uint32_t phase, std::string_view reason, bool reused = false);
  void Sun(uint64_t pixel, const SunShaftParameters& parameters, const EnvironmentalDataV2* environment,
           bool encoded, std::string_view reason);
  void Cloud(uint64_t pixel, bool ready, uint32_t width, uint32_t height);
 private:
  void Summary();
  using Counts = std::array<std::array<uint64_t, size_t(ModernExecution::kCount)>, size_t(ModernEffect::kCount)>;
  Counts counts_{};
  std::array<ModernExecution, size_t(ModernEffect::kCount)> last_{};
  std::array<uint32_t, size_t(ModernEffect::kCount)> changes_{};
  bool enabled_ = false;
  ModernShaderSettings settings_{};
  const char* backend_ = "unknown";
  uint64_t frame_ = 0, window_start_ = 0;
  ModernDiagnosticGate failures_, sky_;
};
}  // namespace rex::graphics::gta4_native
