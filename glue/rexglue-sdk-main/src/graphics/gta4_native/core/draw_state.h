#include <rex/graphics/gta4_native/modern_effect_constants.h>
#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <rex/graphics/gta4_native/title_commands.h>
#include "../alpha_to_coverage_util.h"
#include "../native_clip_control.h"
#include "../native_color_output.h"
#include "../native_fixed_function_policy.h"

namespace rex::graphics::gta4_native::core {
// Exact retail-state layout shared with the native Vulkan capture. This header
// contains game semantics only: no API handles, allocation or synchronization.
struct FixedFunctionState {
    uint32_t depth_enable = 0;
    uint32_t depth_function = 0;
    uint32_t depth_write_enable = 0;
    uint32_t depth_clamp_enable = 0;
    uint32_t clip_control = 0;
    uint32_t user_clip_plane_enable_mask = 0;
    std::array<uint32_t, 4> clip_plane_bits{};
    uint32_t negative_one_to_one_clip_space = 0;
    uint32_t cull_mode = 0;
    uint32_t polygon_mode = 0;
    uint32_t blend_enable = 0;
    std::array<uint32_t, kRenderTargetCount> blend_controls{};
    uint32_t source_blend = 0;
    uint32_t destination_blend = 0;
    uint32_t blend_operation = 0;
    uint32_t source_blend_alpha = 0;
    uint32_t destination_blend_alpha = 0;
    uint32_t blend_operation_alpha = 0;
    std::array<float, 4> blend_constants{};
    uint32_t alpha_test_enable = 0;
    uint32_t alpha_function = 0;
    float alpha_reference = 0.0f;
    // Xenos RB_COLORCONTROL bit 4. This is deliberately captured separately
    // from alpha test: foliage may request alpha-to-mask while alpha test is
    // disabled.
    uint32_t alpha_to_mask_enable = 0;
    // Native shader layout: four RB_COLORCONTROL offsets in bits 0-7 and the
    // enable in bit 8. This remains dynamic and is not a pipeline-key field.
    uint32_t alpha_to_mask = 0;
    uint32_t stencil_enable = 0;
    uint32_t two_sided_stencil = 0;
    uint32_t stencil_fail = 0;
    uint32_t stencil_depth_fail = 0;
    uint32_t stencil_pass = 0;
    uint32_t stencil_function = 0;
    uint32_t stencil_reference = 0;
    uint32_t stencil_mask = 0;
    uint32_t stencil_write_mask = 0;
    uint32_t back_stencil_reference = 0;
    uint32_t back_stencil_mask = 0;
    uint32_t back_stencil_write_mask = 0;
    uint32_t ccw_stencil_fail = 0;
    uint32_t ccw_stencil_depth_fail = 0;
    uint32_t ccw_stencil_pass = 0;
    uint32_t ccw_stencil_function = 0;
    uint32_t scissor_enable = 0;
    uint32_t slope_scaled_depth_bias_bits = 0;
    uint32_t depth_bias_bits = 0;
    bool depth_bias_enable = false;
    bool depth_bias_representable = true;
    uint32_t color_write_mask = 0;
    uint32_t sample_mask = 0xFFFFu;
    std::array<uint32_t, 6> viewport_bits{};
    std::array<int32_t, 4> scissor{};

    bool operator==(const FixedFunctionState&) const = default;
  };
struct SharedConstants {
  uint32_t texture_2d_indices[kTextureStageCount]{};
  uint32_t texture_2d_array_indices[kTextureStageCount]{};
  uint32_t texture_3d_indices[kTextureStageCount]{};
  uint32_t texture_cube_indices[kTextureStageCount]{};
  uint32_t sampler_indices[kTextureStageCount]{};
  // Five 26-word descriptor arrays occupy 130 words. Pad the descriptor block to the next
  // 16-byte constant-buffer register so the raw SPIR-V/AIR loads and HLSL packoffset layout are
  // identical.
  uint32_t descriptor_padding[2]{};
  uint32_t booleans = 0;
  uint32_t swapped_texcoords = 0;
  uint32_t swapped_normals = 0;
  uint32_t swapped_binormals = 0;
  uint32_t swapped_tangents = 0;
  uint32_t swapped_blend_weights = 0;
  float half_pixel_offset_x = 0.0f;
  float half_pixel_offset_y = 0.0f;
  float clip_plane[4]{};
  uint8_t clip_plane_enabled = 0;
  uint8_t clip_plane_padding[3]{};
  float alpha_threshold = 0.0f;
  uint32_t conditional_survey_index = 0;
  uint32_t conditional_rendering_index = 0;
  // GTA IV authored its directional motion blur around a 30 Hz frame. Exact
  // composite shader overrides consume this correction from the next complete
  // constant-buffer register; stock shaders never address it.
  float motion_blur_time_scale = 1.0f;
  // Set per draw only after the Vulkan or Metal split pass succeeds.
  float split_postfx_applied = 0.0f;
  float motion_blur_padding[2]{};
  // Fusion-compatible exponential-height fog parameters followed by the
  // camera, projection scale, and guest row-major inverse-view matrix. Shader overrides use
  // the validity mask before consuming any captured guest state.
  float fog_parameters[4]{};
  float camera_position[4]{};
  float view_inverse_matrix[16]{};
  float projection_scale[4]{};
  uint64_t environmental_valid_fields = 0;
  uint64_t environmental_padding = 0;
  // Xenos alpha-to-mask is shader-generated so its title-selected 2x2
  // threshold offsets and actual host attachment sample count remain exact.
  uint32_t alpha_to_mask = 0;
  uint32_t alpha_to_mask_sample_count = 1;
  float fragment_coordinate_scale_x = 1.0f;
  float fragment_coordinate_scale_y = 1.0f;
  float sampler_lod_bias[kTextureStageCount]{};
  float sampler_lod_bias_padding[2]{};
  std::array<NativeColorOutputParameters, kNativeColorOutputTargetCount> color_output{};
  ModernEffectConstants modern_effects{};
};
static_assert(offsetof(SharedConstants, texture_2d_indices) == 0x000);
static_assert(offsetof(SharedConstants, texture_2d_array_indices) == 0x068);
static_assert(offsetof(SharedConstants, texture_3d_indices) == 0x0D0);
static_assert(offsetof(SharedConstants, texture_cube_indices) == 0x138);
static_assert(offsetof(SharedConstants, sampler_indices) == 0x1A0);
static_assert(offsetof(SharedConstants, booleans) == 0x210);
static_assert(offsetof(SharedConstants, clip_plane) == 0x230);
static_assert(offsetof(SharedConstants, clip_plane_enabled) == 0x240);
static_assert(offsetof(SharedConstants, alpha_threshold) == 0x244);
static_assert(offsetof(SharedConstants, conditional_survey_index) == 0x248);
static_assert(offsetof(SharedConstants, conditional_rendering_index) == 0x24C);
static_assert(offsetof(SharedConstants, motion_blur_time_scale) == 0x250);
static_assert(offsetof(SharedConstants, fog_parameters) == 0x260);
static_assert(offsetof(SharedConstants, camera_position) == 0x270);
static_assert(offsetof(SharedConstants, view_inverse_matrix) == 0x280);
static_assert(offsetof(SharedConstants, projection_scale) == 0x2C0);
static_assert(offsetof(SharedConstants, environmental_valid_fields) == 0x2D0);
static_assert(offsetof(SharedConstants, alpha_to_mask) == 0x2E0);
static_assert(offsetof(SharedConstants, alpha_to_mask_sample_count) == 0x2E4);
static_assert(offsetof(SharedConstants, fragment_coordinate_scale_x) == 0x2E8);
static_assert(offsetof(SharedConstants, sampler_lod_bias) == 0x2F0);
static_assert(offsetof(SharedConstants, color_output) == kNativeColorOutputOffset);
static_assert(offsetof(SharedConstants, modern_effects) == kModernEffectConstantsOffset);
static_assert(sizeof(SharedConstants) == 0x500);
constexpr uint32_t kDepthStencilOffset = 12448;
constexpr uint32_t kResourceDataOffset = 24;
constexpr uint32_t kResourceSizeOffset = 28;
constexpr uint32_t kIndex32Flag = 0x80000000;
constexpr uint32_t kD3dFmtA8R8G8B8 = 0x18280186;
constexpr uint32_t kD3dFmtA16B16G16R16F2 = 0x1A2201BF;
constexpr uint32_t kD3dFmtD24FS8 = 0x1A220197;
constexpr uint32_t kD3dFmtR32F = 0x2DA2ABA4;
constexpr uint32_t kD3dFmtG16R16F = 0x2D22AB9F;
constexpr uint32_t kD3dFmtG16R16F2 = 0x2D20AB8D;
constexpr uint32_t kClipPlane0Offset = 10272;
constexpr uint32_t kPackedWindowOffset = 10432;
constexpr uint32_t kPackedScissorTopLeftOffset = 10436;
constexpr uint32_t kPackedScissorBottomRightOffset = 10440;
constexpr uint32_t kPackedColorWriteMaskOffset = 10460;
constexpr uint32_t kBlendConstantsOffset = 10464;
constexpr uint32_t kAlphaReferenceOffset = 10500;
constexpr std::array<uint32_t, kRenderTargetCount> kHardwareBlendPackedOffsets = {
    10552, 10584, 10588, 10592};
constexpr uint32_t kDepthStencilPackedOffset = 10548;
constexpr uint32_t kAlphaTestFunctionPackedOffset = 10556;
constexpr uint32_t kClipControlPackedOffset = 10564;
constexpr uint32_t kCullModePackedOffset = 10568;
constexpr uint32_t kSampleMaskOffset = 10752;
constexpr uint32_t kDepthBiasOffset = 10832;
constexpr uint32_t kSlopeScaledDepthBiasOffset = 10836;
constexpr uint32_t kBackDepthBiasOffset = 10840;
constexpr uint32_t kBackSlopeScaledDepthBiasOffset = 10844;
constexpr uint32_t kBlendControlOffset = 11844;
constexpr uint32_t kScissorEnableOffset = 11848;
constexpr uint32_t kDepthEnableOffset = 11868;
constexpr uint32_t kStencilEnableOffset = 11872;
constexpr uint32_t kStencilReferenceOffset = 10499;
constexpr uint32_t kStencilMaskOffset = 10498;
constexpr uint32_t kStencilWriteMaskOffset = 10497;
constexpr uint32_t kBackStencilReferenceOffset = 10495;
constexpr uint32_t kBackStencilMaskOffset = 10494;
constexpr uint32_t kBackStencilWriteMaskOffset = 10493;
constexpr uint32_t kViewportXOffset = 12640;
constexpr uint32_t kViewportYOffset = 12644;
constexpr uint32_t kViewportWidthOffset = 12648;
constexpr uint32_t kViewportHeightOffset = 12652;
constexpr uint32_t kViewportMinDepthOffset = 12656;
constexpr uint32_t kViewportMaxDepthOffset = 12660;
constexpr uint32_t kScissorLeftOffset = 12668;
constexpr uint32_t kScissorTopOffset = 12672;
constexpr uint32_t kScissorRightOffset = 12676;
constexpr uint32_t kScissorBottomOffset = 12680;

inline uint32_t LoadGuestWord(std::span<const uint8_t> bytes, size_t offset) {
  if (offset > bytes.size() || sizeof(uint32_t) > bytes.size()-offset) return 0;
  uint32_t word;
  std::memcpy(&word,bytes.data()+offset,sizeof(word));
  return __builtin_bswap32(word);
}
inline FixedFunctionState DecodeFixedState(std::span<const uint8_t> device_snapshot) {
  FixedFunctionState state{};
  // sub_82A50160 installs getter function pointers at device + 548 + state.
  // The getters themselves read these packed, authoritative device fields.
  const uint32_t depth_stencil = LoadGuestWord(device_snapshot, kDepthStencilPackedOffset);
  const uint32_t alpha_test_function =
      LoadGuestWord(device_snapshot, kAlphaTestFunctionPackedOffset);
  const uint32_t clip_control = LoadGuestWord(device_snapshot, kClipControlPackedOffset);
  const uint32_t setup_mode = LoadGuestWord(device_snapshot, kCullModePackedOffset);
  const uint32_t blend_control = LoadGuestWord(device_snapshot, kBlendControlOffset);

  state.depth_enable = LoadGuestWord(device_snapshot, kDepthEnableOffset);
  state.depth_function = (depth_stencil & 0x00000070) >> 4;
  state.depth_write_enable = (depth_stencil & 0x00000004) >> 2;
  state.depth_clamp_enable = IsNativeDepthClampRequested(clip_control) ? 1u : 0u;
  state.clip_control = clip_control;
  state.user_clip_plane_enable_mask = NativeUserClipPlaneEnableMask(clip_control);
  for (uint32_t component = 0; component < state.clip_plane_bits.size(); ++component) {
    state.clip_plane_bits[component] =
        LoadGuestWord(device_snapshot, kClipPlane0Offset + component * sizeof(uint32_t));
  }
  state.negative_one_to_one_clip_space =
      IsNativeNegativeOneToOneClipSpace(clip_control) ? 1u : 0u;

  state.cull_mode = setup_mode & 0x7u;
  state.polygon_mode = uint32_t(SelectNativePolygonMode(setup_mode, state.cull_mode));

  state.blend_enable = (blend_control & 0x80000000u) >> 31;
  for (uint32_t target = 0; target < state.blend_controls.size(); ++target) {
    state.blend_controls[target] =
        LoadGuestWord(device_snapshot, kHardwareBlendPackedOffsets[target]);
  }
  const NativeBlendControlState primary_blend =
      DecodeNativeBlendControl(state.blend_controls[0]);
  // Keep the existing RT0 fields for bounded diagnostics and attribution.
  state.source_blend = primary_blend.source_color;
  state.destination_blend = primary_blend.destination_color;
  state.blend_operation = primary_blend.color_operation;
  state.source_blend_alpha = primary_blend.source_alpha;
  state.destination_blend_alpha = primary_blend.destination_alpha;
  state.blend_operation_alpha = primary_blend.alpha_operation;
  for (uint32_t component = 0; component < state.blend_constants.size(); ++component) {
    state.blend_constants[component] = std::bit_cast<float>(
        LoadGuestWord(device_snapshot, kBlendConstantsOffset + component * sizeof(uint32_t)));
  }

  state.alpha_test_enable = (alpha_test_function & 0x00000008) >> 3;
  state.alpha_function = alpha_test_function & 0x7;
  state.alpha_reference =
      std::bit_cast<float>(LoadGuestWord(device_snapshot, kAlphaReferenceOffset));
  state.alpha_to_mask_enable = (alpha_test_function & 0x00000010) >> 4;
  state.alpha_to_mask = PackNativeAlphaToMask(alpha_test_function);

  state.stencil_enable = LoadGuestWord(device_snapshot, kStencilEnableOffset);
  state.two_sided_stencil = (depth_stencil & 0x00000080) >> 7;
  state.stencil_function = (depth_stencil & 0x00000700) >> 8;
  state.stencil_fail = (depth_stencil & 0x00003800) >> 11;
  state.stencil_pass = (depth_stencil & 0x0001C000) >> 14;
  state.stencil_depth_fail = (depth_stencil & 0x000E0000) >> 17;
  state.ccw_stencil_function = (depth_stencil & 0x00700000) >> 20;
  state.ccw_stencil_fail = (depth_stencil & 0x03800000) >> 23;
  state.ccw_stencil_pass = (depth_stencil & 0x1C000000) >> 26;
  state.ccw_stencil_depth_fail = (depth_stencil & 0xE0000000) >> 29;
  state.stencil_reference = device_snapshot[kStencilReferenceOffset];
  state.stencil_mask = device_snapshot[kStencilMaskOffset];
  state.stencil_write_mask = device_snapshot[kStencilWriteMaskOffset];
  state.back_stencil_reference = device_snapshot[kBackStencilReferenceOffset];
  state.back_stencil_mask = device_snapshot[kBackStencilMaskOffset];
  state.back_stencil_write_mask = device_snapshot[kBackStencilWriteMaskOffset];

  state.scissor_enable = LoadGuestWord(device_snapshot, kScissorEnableOffset);
  const NativeDepthBiasFaceState front_depth_bias = {
      (setup_mode & 0x00000800u) != 0,
      LoadGuestWord(device_snapshot, kSlopeScaledDepthBiasOffset),
      LoadGuestWord(device_snapshot, kDepthBiasOffset),
  };
  const NativeDepthBiasFaceState back_depth_bias = {
      (setup_mode & 0x00001000u) != 0,
      LoadGuestWord(device_snapshot, kBackSlopeScaledDepthBiasOffset),
      LoadGuestWord(device_snapshot, kBackDepthBiasOffset),
  };
  const NativeDepthBiasSelection depth_bias =
      SelectNativeDepthBias(state.cull_mode, front_depth_bias, back_depth_bias);
  state.depth_bias_representable = depth_bias.representable;
  if (depth_bias.representable) {
    state.depth_bias_enable = depth_bias.selected.enabled;
    state.slope_scaled_depth_bias_bits = depth_bias.selected.slope_bits;
    state.depth_bias_bits = depth_bias.selected.constant_bits;
  }

  state.color_write_mask =
      LoadGuestWord(device_snapshot, kPackedColorWriteMaskOffset) & 0xFFFFu;
  state.sample_mask = LoadGuestWord(device_snapshot, kSampleMaskOffset) & 0xFFFFu;
  state.viewport_bits = {
      LoadGuestWord(device_snapshot, kViewportXOffset),
      LoadGuestWord(device_snapshot, kViewportYOffset),
      LoadGuestWord(device_snapshot, kViewportWidthOffset),
      LoadGuestWord(device_snapshot, kViewportHeightOffset),
      LoadGuestWord(device_snapshot, kViewportMinDepthOffset),
      LoadGuestWord(device_snapshot, kViewportMaxDepthOffset),
  };
  const NativePackedScissorState packed_scissor = DecodeNativePackedScissor(
      LoadGuestWord(device_snapshot, kPackedScissorTopLeftOffset),
      LoadGuestWord(device_snapshot, kPackedScissorBottomRightOffset),
      LoadGuestWord(device_snapshot, kPackedWindowOffset));
  state.scissor = packed_scissor.rectangle;
  return state;
}
}
