#pragma once

#include <cstdint>
#include "shader_override_policy.h"

namespace rex::graphics::gta4_native {

// Exact stage/hash entries in LibertyRecompLib/shader_overrides/manifest.json.
// Video translations are not visual upgrades and never belong to the toggle.
enum class ModernShaderFamily : uint8_t {
  kOther,
  kMotionBlur,
  kDepthOfField,
  kTladGrain,
  kBloom,
  kWater,
  kLightVolume,
  kFog,
  kSky,
  kVideo,
  kCount
};
constexpr ModernShaderFamily ClassifyModernShader(uint64_t hash) {
  switch (hash) {
#include "fusion_forward_shader_hashes.inc"
    case 0xEE75C9F6AA1AB16Aull:
    case 0xE51D9DD95A333D92ull:
    case 0xD3B2B2125BC24911ull:
    case 0x94CC5AF0E2FF5B08ull:
    case 0x535ACDAB8AE84D82ull:
      return ModernShaderFamily::kMotionBlur;
    case 0xCC0C2F3146CCC96Eull:
    case 0x2EAEDEB9125948C2ull:
    case 0x54FABC991DB485C8ull:
    case 0x6533D90E6317B046ull:
    case 0x9568A3A7BD3CDE18ull:
    case 0x8F2ECB251AE7FE9Aull:
    case 0x5D2A71133DA823F7ull:
    case 0x7756A65D296806FFull:
    case 0xFE2FC0894C018D11ull:
    case 0x9649029E1999BEA9ull:
    case 0x069287E9B706AA14ull:
    case 0x53906ADBE74441C6ull:
    case 0xB44879A571332324ull:
      return ModernShaderFamily::kDepthOfField;
    case 0x79044EA1461439CAull:
    case 0xC5D3E7806E478A16ull:
    case 0x67C1FB770BB55E69ull:
    case 0xE4AF188ACBDA7362ull:
      return ModernShaderFamily::kTladGrain;
    case 0xE74BFC50127AFED3ull:
    case 0x86CB9F8D0850ABE3ull:
      return ModernShaderFamily::kBloom;
    case 0xC3256D6D7C2E426Dull:
      return ModernShaderFamily::kWater;
    case 0xDF64C22EC010C136ull:
    case 0x458818340E2283DEull:
      return ModernShaderFamily::kLightVolume;
    case 0x0421316FC8CF1313ull:
    case 0x439458C53B3FF810ull:
    case 0x000B4D6AC54D11CCull:
      return ModernShaderFamily::kSky;
    case 0xEFAACEED3DBD802Dull:
    case 0x1A6669BBAFDC43E7ull:
      return ModernShaderFamily::kFog;
    case 0x156BAD4A9EE62726ull:
    case 0x9E76B68B60127349ull:
    case 0xA6C9E2B8B2A59D7Aull:
      return ModernShaderFamily::kVideo;
    default:
      return ModernShaderFamily::kOther;
  }
}
constexpr bool IsFusionSkyShader(uint64_t hash) {
  return hash == 0x439458C53B3FF810ull || hash == 0x000B4D6AC54D11CCull;
}
constexpr bool SupportsSplitPostFx(uint64_t hash) {
  constexpr uint64_t composites[] = {
      0x069287E9B706AA14ull,
      0x2EAEDEB9125948C2ull,
      0x535ACDAB8AE84D82ull,
      0x53906ADBE74441C6ull,
      0x54FABC991DB485C8ull,
      0x5D2A71133DA823F7ull,
      0x6533D90E6317B046ull,
      0x67C1FB770BB55E69ull,
      0x7756A65D296806FFull,
      0x79044EA1461439CAull,
      0x8F2ECB251AE7FE9Aull,
      0x9568A3A7BD3CDE18ull,
      0x9649029E1999BEA9ull,
      0xB44879A571332324ull,
      0xC5D3E7806E478A16ull,
      0xCC0C2F3146CCC96Eull,
      0xE4AF188ACBDA7362ull,
      0xE51D9DD95A333D92ull,
      0xEE75C9F6AA1AB16Aull,
      0xFE2FC0894C018D11ull,
  };
  for (auto composite : composites) if (hash == composite) return true;
  return false;
}

constexpr const char* ModernShaderFamilyName(ModernShaderFamily family) {
  switch (family) {
    case ModernShaderFamily::kMotionBlur:
      return "motion-blur";
    case ModernShaderFamily::kDepthOfField:
      return "depth-of-field";
    case ModernShaderFamily::kTladGrain:
      return "tlad-film-grain";
    case ModernShaderFamily::kBloom:
      return "bloom";
    case ModernShaderFamily::kWater:
      return "water";
    case ModernShaderFamily::kLightVolume:
      return "light-volume";
    case ModernShaderFamily::kSky:
      return "sky";
    case ModernShaderFamily::kFog:
      return "fog";
    case ModernShaderFamily::kVideo:
      return "video";
    default:
      return "other";
  }
}
constexpr bool IsModernShader(ModernShaderFamily family) {
  return family != ModernShaderFamily::kOther && family != ModernShaderFamily::kVideo &&
         family != ModernShaderFamily::kCount;
}
struct ModernShaderSettings {
  bool enabled = false;
  bool disable_tlad_grain = false;
  constexpr uint32_t key() const { return uint32_t(enabled) | (uint32_t(disable_tlad_grain) << 1); }
  constexpr bool operator==(const ModernShaderSettings&) const = default;
};
constexpr bool AllowModernShader(uint64_t hash, ModernShaderSettings settings) {
  const auto family = ClassifyModernShader(hash);
  // The authored motion-blur replacements are compatibility/performance
  // translations of the stock composite, not an optional visual redesign.
  // Motion Blur itself is selected by the title presentation option.
  if (family == ModernShaderFamily::kMotionBlur)
    return true;
  if (!IsModernShader(family))
    return true;
  // This is an independent renderer-side veto. The guest pass-selection hook
  // also selects the grain-free pass and its matching sampler layout.
  if (family == ModernShaderFamily::kTladGrain && settings.disable_tlad_grain)
    return false;
  return settings.enabled;
}

// Render-worker-owned. Texture-lock/readback flushes intentionally do not end
// this scope; only the real guest present or a device reset can do that.
class ModernShaderFramePolicy {
 public:
  bool Begin(ModernShaderSettings requested) {
    if (active_)
      return false;
    active_ = true;
    const bool changed = !initialized_ || settings_ != requested;
    settings_ = requested;
    initialized_ = true;
    return changed;
  }
  void EndGuestFrame() { active_ = false; }
  void Reset() {
    active_ = false;
    initialized_ = false;
  }
  bool active() const { return active_; }
  ModernShaderSettings settings() const { return settings_; }

 private:
  ModernShaderSettings settings_{};
  bool active_ = false, initialized_ = false;
};

constexpr ShaderOverrideSelection ResolveModernShaderSelection(ShaderOverrideMode mode,
                                                               ModernShaderSettings settings,
                                                               ShaderOverrideCandidate vertex,
                                                               ShaderOverrideCandidate pixel,
                                                               uint32_t samples) {
  vertex.present = vertex.present && AllowModernShader(vertex.shader_hash, settings) &&
                   ShaderOverrideSupportsSampleCount(vertex, samples);
  pixel.present = pixel.present && AllowModernShader(pixel.shader_hash, settings) &&
                  ShaderOverrideSupportsSampleCount(pixel, samples);
  constexpr uint64_t fog_vs = 0xEFAACEED3DBD802Dull, fog_ps = 0x1A6669BBAFDC43E7ull;
  // The fog PS reads a view ray produced by the fog VS. The VS is otherwise
  // shared by stock fullscreen effects: never inject half of the new interface.
  const bool fog_pair = vertex.shader_hash == fog_vs && pixel.shader_hash == fog_ps &&
                        vertex.present && pixel.present;
  if (vertex.shader_hash == fog_vs)
    vertex.present = fog_pair;
  if (pixel.shader_hash == fog_ps)
    pixel.present = fog_pair;
  auto selection = ResolveShaderOverrideSelection(mode, vertex, pixel, samples);
  if (ClassifyModernShader(vertex.shader_hash) == ModernShaderFamily::kLightVolume ||
      ClassifyModernShader(pixel.shader_hash) == ModernShaderFamily::kLightVolume) {
    // Even the legacy independent-stage diagnostic mode must not turn a Modern
    // shaders toggle into an unmatched geometry/coverage pair.
    selection = ResolveShaderOverrideSelection(
        mode == ShaderOverrideMode::kStock ? mode : ShaderOverrideMode::kPair, vertex, pixel,
        samples);
  }
  return selection;
}
}  // namespace rex::graphics::gta4_native
