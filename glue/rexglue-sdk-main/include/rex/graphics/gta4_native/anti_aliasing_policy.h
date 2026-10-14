#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace rex::graphics::gta4_native {

// Public Advanced Graphics values. Legacy spellings are deliberately excluded
// from this enum and are handled only by ResolveAntiAliasingConfiguration.
enum class AntiAliasingMode : uint8_t {
  kOff,
  kFxaa,
  kSmaa,
  kMsaa2x,
  kMsaa4x,
  kSsaa2x,
  kSsaa4x,
  kSsaa6x,
  kSsaa8x,
  kSsaa10x,
  kSsaa12x,
  kSsaa14x,
  kSsaa16x,
  kMsaa4xSmaa,  // Four scene samples, then SMAA 1x on the resolved title image.
  kTaa,
  kMetalFxTaa,
};

enum class AntiAliasingApplyResult : uint8_t {
  kRejected,
  kAppliedLive,
  kRestartRequired,
};

enum class AntiAliasingCompatibility : uint8_t {
  kCanonical,
  kSpatialAlias,
  kLegacySceneMsaa,
  kLegacySpatialToggle,
  kFallbackOff,
};

struct ResolvedAntiAliasingConfiguration {
  AntiAliasingMode mode = AntiAliasingMode::kOff;
  AntiAliasingCompatibility compatibility = AntiAliasingCompatibility::kCanonical;
  bool ignored_legacy_scene_msaa = false;
};

struct AntiAliasingRoute {
  bool presentation_fxaa = false;
  bool presentation_smaa = false;
  uint32_t scene_sample_count = 1u;
  // Total scene pixel-count multiplier. Unlike MSAA, this increases the
  // physical width and height of host-only render targets and is resolved
  // back to the logical output extent before presentation.
  uint32_t supersampling_pixel_factor = 1u;
  uint32_t temporal_method = 0u; // 1: native TAA; 2: MetalFX temporal AA.
};

constexpr std::optional<AntiAliasingMode> ParseAntiAliasingMode(std::string_view value) {
  if (value == "taa") return AntiAliasingMode::kTaa;
  if (value == "metalfx_taa") return AntiAliasingMode::kMetalFxTaa;
  if (value == "off") {
    return AntiAliasingMode::kOff;
  }
  if (value == "fxaa") {
    return AntiAliasingMode::kFxaa;
  }
  if (value == "smaa") {
    return AntiAliasingMode::kSmaa;
  }
  if (value == "msaa2x") {
    return AntiAliasingMode::kMsaa2x;
  }
  if (value == "msaa4x") {
    return AntiAliasingMode::kMsaa4x;
  }
  if (value == "msaa4x_smaa") {
    return AntiAliasingMode::kMsaa4xSmaa;
  }
  if (value == "ssaa2x") {
    return AntiAliasingMode::kSsaa2x;
  }
  if (value == "ssaa4x") {
    return AntiAliasingMode::kSsaa4x;
  }
  if (value == "ssaa6x") {
    return AntiAliasingMode::kSsaa6x;
  }
  if (value == "ssaa8x") {
    return AntiAliasingMode::kSsaa8x;
  }
  if (value == "ssaa10x") {
    return AntiAliasingMode::kSsaa10x;
  }
  if (value == "ssaa12x") {
    return AntiAliasingMode::kSsaa12x;
  }
  if (value == "ssaa14x") {
    return AntiAliasingMode::kSsaa14x;
  }
  if (value == "ssaa16x") {
    return AntiAliasingMode::kSsaa16x;
  }
  return std::nullopt;
}

constexpr std::string_view AntiAliasingModeName(AntiAliasingMode mode) {
  switch (mode) {
    case AntiAliasingMode::kTaa: return "taa";
    case AntiAliasingMode::kMetalFxTaa: return "metalfx_taa";
    case AntiAliasingMode::kOff:
      return "off";
    case AntiAliasingMode::kFxaa:
      return "fxaa";
    case AntiAliasingMode::kSmaa:
      return "smaa";
    case AntiAliasingMode::kMsaa2x:
      return "msaa2x";
    case AntiAliasingMode::kMsaa4x:
      return "msaa4x";
    case AntiAliasingMode::kMsaa4xSmaa:
      return "msaa4x_smaa";
    case AntiAliasingMode::kSsaa2x:
      return "ssaa2x";
    case AntiAliasingMode::kSsaa4x:
      return "ssaa4x";
    case AntiAliasingMode::kSsaa6x:
      return "ssaa6x";
    case AntiAliasingMode::kSsaa8x:
      return "ssaa8x";
    case AntiAliasingMode::kSsaa10x:
      return "ssaa10x";
    case AntiAliasingMode::kSsaa12x:
      return "ssaa12x";
    case AntiAliasingMode::kSsaa14x:
      return "ssaa14x";
    case AntiAliasingMode::kSsaa16x:
      return "ssaa16x";
  }
  return "off";
}

constexpr bool UsesTemporalAntiAliasing(AntiAliasingMode mode) {
  return mode == AntiAliasingMode::kTaa || mode == AntiAliasingMode::kMetalFxTaa;
}

constexpr bool UsesSceneMsaa(AntiAliasingMode mode) {
  return mode == AntiAliasingMode::kMsaa2x || mode == AntiAliasingMode::kMsaa4x ||
         mode == AntiAliasingMode::kMsaa4xSmaa;
}

constexpr bool UsesSceneSupersampling(AntiAliasingMode mode) {
  return mode >= AntiAliasingMode::kSsaa2x && mode <= AntiAliasingMode::kSsaa16x;
}

constexpr bool UsesSceneTopology(AntiAliasingMode mode) {
  return UsesSceneMsaa(mode) || UsesSceneSupersampling(mode) || UsesTemporalAntiAliasing(mode);
}

constexpr AntiAliasingRoute GetAntiAliasingRoute(AntiAliasingMode mode) {
  switch (mode) {
    case AntiAliasingMode::kFxaa:
      return {.presentation_fxaa = true};
    case AntiAliasingMode::kSmaa:
      return {.presentation_smaa = true};
    case AntiAliasingMode::kMsaa2x:
      return {.scene_sample_count = 2u};
    case AntiAliasingMode::kMsaa4x:
      return {.scene_sample_count = 4u};
    case AntiAliasingMode::kMsaa4xSmaa:
      return {.presentation_smaa = true, .scene_sample_count = 4u};
    case AntiAliasingMode::kSsaa2x:
      return {.supersampling_pixel_factor = 2u};
    case AntiAliasingMode::kSsaa4x:
      return {.supersampling_pixel_factor = 4u};
    case AntiAliasingMode::kSsaa6x:
      return {.supersampling_pixel_factor = 6u};
    case AntiAliasingMode::kSsaa8x:
      return {.supersampling_pixel_factor = 8u};
    case AntiAliasingMode::kSsaa10x:
      return {.supersampling_pixel_factor = 10u};
    case AntiAliasingMode::kSsaa12x:
      return {.supersampling_pixel_factor = 12u};
    case AntiAliasingMode::kSsaa14x:
      return {.supersampling_pixel_factor = 14u};
    case AntiAliasingMode::kSsaa16x:
      return {.supersampling_pixel_factor = 16u};
    case AntiAliasingMode::kTaa: return {.temporal_method = 1u};
    case AntiAliasingMode::kMetalFxTaa: return {.temporal_method = 2u};
    case AntiAliasingMode::kOff:
      return {};
  }
  return {};
}

// Validate supported stage combinations, including the explicit hybrid mode.
constexpr bool IsValidAntiAliasingRoute(const AntiAliasingRoute& route) {
  const bool scene_msaa = route.scene_sample_count > 1u;
  const bool scene_ssaa = route.supersampling_pixel_factor > 1u;
  const bool sample_count_valid = route.scene_sample_count == 1u ||
      route.scene_sample_count == 2u || route.scene_sample_count == 4u;
  const bool pixel_factor_valid = route.supersampling_pixel_factor == 1u ||
      (route.supersampling_pixel_factor >= 2u && route.supersampling_pixel_factor <= 16u &&
       (route.supersampling_pixel_factor & 1u) == 0u);
  const bool temporal_valid = route.temporal_method <= 2u && (!route.temporal_method ||
      (!scene_msaa && !scene_ssaa && !route.presentation_fxaa && !route.presentation_smaa));
  return sample_count_valid && pixel_factor_valid && temporal_valid &&
         !(route.presentation_fxaa && route.presentation_smaa) &&
         !(route.presentation_fxaa && scene_msaa) &&
         !(route.presentation_smaa && scene_msaa && route.scene_sample_count != 4u) &&
         !(route.presentation_fxaa && scene_ssaa) &&
         !(route.presentation_smaa && scene_ssaa) && !(scene_msaa && scene_ssaa);
}

constexpr bool CanApplyAntiAliasingLive(AntiAliasingMode active,
                                        AntiAliasingMode requested) {
  const auto before = GetAntiAliasingRoute(active);
  const auto after = GetAntiAliasingRoute(requested);
  // A post-process toggle can be live when the physical scene topology is unchanged.
  return before.scene_sample_count == after.scene_sample_count &&
         before.supersampling_pixel_factor == after.supersampling_pixel_factor &&
         before.temporal_method == after.temporal_method;
}

// Old configurations stored final-image AA and scene MSAA independently.
// Preserve them deterministically: an explicit post-process mode wins, the
// removed spatial mode becomes FXAA, and only legacy `off` consults the old
// scene-MSAA setting. The old boolean is a last-resort fallback for malformed
// or pre-string configurations.
constexpr ResolvedAntiAliasingConfiguration ResolveAntiAliasingConfiguration(
    std::string_view configured_mode, std::string_view legacy_scene_msaa,
    bool legacy_spatial_enabled, bool unified_mode_selected = false) {
  if (const auto mode = ParseAntiAliasingMode(configured_mode); mode && UsesTemporalAntiAliasing(*mode))
    return {*mode, AntiAliasingCompatibility::kCanonical, true};
  if (unified_mode_selected) {
    if (const auto canonical = ParseAntiAliasingMode(configured_mode)) {
      return {*canonical, AntiAliasingCompatibility::kCanonical,
              legacy_scene_msaa == "2x" || legacy_scene_msaa == "4x"};
    }
  }
  if (configured_mode == "fxaa") {
    return {AntiAliasingMode::kFxaa, AntiAliasingCompatibility::kCanonical,
            legacy_scene_msaa == "2x" || legacy_scene_msaa == "4x"};
  }
  if (configured_mode == "smaa") {
    return {AntiAliasingMode::kSmaa, AntiAliasingCompatibility::kCanonical,
            legacy_scene_msaa == "2x" || legacy_scene_msaa == "4x"};
  }
  if (configured_mode == "msaa2x") {
    return {AntiAliasingMode::kMsaa2x, AntiAliasingCompatibility::kCanonical,
            legacy_scene_msaa == "4x"};
  }
  if (configured_mode == "msaa4x_smaa") {
    return {AntiAliasingMode::kMsaa4xSmaa, AntiAliasingCompatibility::kCanonical,
            legacy_scene_msaa == "2x"};
  }
  if (configured_mode == "msaa4x") {
    return {AntiAliasingMode::kMsaa4x, AntiAliasingCompatibility::kCanonical,
            legacy_scene_msaa == "2x"};
  }
  if (const auto canonical = ParseAntiAliasingMode(configured_mode);
      canonical && UsesSceneSupersampling(*canonical)) {
    return {*canonical, AntiAliasingCompatibility::kCanonical,
            legacy_scene_msaa == "2x" || legacy_scene_msaa == "4x"};
  }
  if (configured_mode == "spatial") {
    return {AntiAliasingMode::kFxaa, AntiAliasingCompatibility::kSpatialAlias,
            legacy_scene_msaa == "2x" || legacy_scene_msaa == "4x"};
  }
  if (configured_mode == "off") {
    if (legacy_scene_msaa == "2x") {
      return {AntiAliasingMode::kMsaa2x, AntiAliasingCompatibility::kLegacySceneMsaa};
    }
    if (legacy_scene_msaa == "4x") {
      return {AntiAliasingMode::kMsaa4x, AntiAliasingCompatibility::kLegacySceneMsaa};
    }
    return {AntiAliasingMode::kOff, AntiAliasingCompatibility::kCanonical};
  }
  if (legacy_spatial_enabled) {
    return {AntiAliasingMode::kFxaa, AntiAliasingCompatibility::kLegacySpatialToggle,
            legacy_scene_msaa == "2x" || legacy_scene_msaa == "4x"};
  }
  return {AntiAliasingMode::kOff, AntiAliasingCompatibility::kFallbackOff,
          legacy_scene_msaa == "2x" || legacy_scene_msaa == "4x"};
}

// Initializes the active renderer route from canonical and legacy config.
// Safe to call repeatedly; initialization is performed once per process.
void InitializeAntiAliasingController();

AntiAliasingMode GetConfiguredAntiAliasingMode();
AntiAliasingMode GetActiveAntiAliasingMode();
std::string_view GetConfiguredAntiAliasingModeName();
std::string_view GetActiveAntiAliasingModeName();

// This is the frontend mutation boundary. Callers must not write the canonical
// cvar directly because MSAA changes require a latched renderer route.
AntiAliasingApplyResult SetConfiguredAntiAliasingMode(std::string_view value);

}  // namespace rex::graphics::gta4_native
