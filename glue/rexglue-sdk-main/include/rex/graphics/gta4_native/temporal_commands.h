#pragma once
#include <array>
#include <type_traits>

#include "title_commands.h"

namespace rex::graphics::gta4_native {
// Optional extension, sent only when the active plugin advertises it. The base
// title ABI and all existing commands retain their layout and numeric values.
inline constexpr uint32_t kCapabilityTemporalInputs = 1u << 1;
inline constexpr uint32_t kCapabilityMetalFxFrameGeneration = 1u << 2;
inline constexpr uint32_t kCapabilityFsr3Upscaling = 1u << 3;
inline constexpr uint32_t kCapabilityDlssUpscaling = 1u << 4;
inline constexpr uint32_t kCapabilityFsrFrameGeneration = 1u << 5;
inline constexpr uint32_t kCapabilityDlssFrameGeneration = 1u << 6;
inline constexpr uint32_t kCapabilityMetalFxTemporalUpscaling = 1u << 7;
inline constexpr uint32_t kCapabilityNativeTemporalAA = 1u << 8;

enum class TemporalUpscalerProvider : uint32_t { kFsr3, kDlss };
enum class TemporalUpscalerQuality : uint32_t {
  kNative, kQuality, kBalanced, kPerformance, kUltraPerformance
};
enum class TemporalUpscalerStatus : uint32_t {
  kUnavailable,
  kReady,
  kUnsupportedPlatform,
  kMissingRuntime,
  kUnsupportedDevice,
  kUnsupportedQuality,
  kIncompleteTemporalInputs,
  kInvalidRequest,
};

// Optional synchronous query. It leaves the guest ABI and earlier command
// layouts intact. The renderer reports vendor-recommended extents and actual
// jitter for this sequence before the title allocates or renders its scene.
struct QueryTemporalUpscalerCommand {
  CommandHeader header{sizeof(QueryTemporalUpscalerCommand), CommandType::kQueryTemporalUpscaler};
  TemporalUpscalerProvider provider = TemporalUpscalerProvider::kFsr3;
  TemporalUpscalerQuality quality = TemporalUpscalerQuality::kQuality;
  uint32_t output_width = 0, output_height = 0;
  uint64_t sequence = 0;
};
struct TemporalUpscalerResult {
  uint32_t render_width = 0, render_height = 0;
  std::array<float, 2> jitter{};
  TemporalUpscalerStatus status = TemporalUpscalerStatus::kUnavailable;
  uint32_t capabilities = 0;
};
static_assert(std::is_trivially_copyable_v<QueryTemporalUpscalerCommand>);
static_assert(std::is_trivially_copyable_v<TemporalUpscalerResult>);
enum class TemporalEvent : uint32_t {
  kBeginScene,
  kBeforePostFx,
  kAfterPostFx,
  kInstance,
  kEndFrame
};
inline constexpr uint32_t kTemporalMainView = 1u << 3;
inline constexpr uint32_t kTemporalExecutionCamera = 1u << 4;
inline constexpr uint32_t kTemporalSceneGeometry = 1u << 5;
inline constexpr uint32_t kTemporalScreenSpace = 1u << 6;
inline constexpr uint32_t kTemporalExecutionAttributed = 1u << 7;
inline constexpr uint32_t kTemporalCompositeExecution = 1u << 8;
inline constexpr uint32_t kTemporalFinalCompositeExecution = 1u << 9;
constexpr uint32_t TemporalCompositeExecutionFlags(const LightingContext& lighting,bool final) {
  if(lighting.stage!=RenderExecutionStage::kCompositePostFx||
      lighting.source_function!=0x822D1710||!lighting.occurrence_id)return 0;
  return kTemporalCompositeExecution|(final?kTemporalFinalCompositeExecution:0);
}
// IDs belong to the executing retail list, not the asynchronous host phase
// stack. Offscreen/reflection and UI lists are deliberately outside the scene
// jitter domain even if they happen to bind a full-size target and camera.
constexpr uint32_t TemporalExecutionFlags(bool attributed, uint32_t retail_phase) {
  if (!attributed) return 0;
  switch (retail_phase) {
    case 14: case 15: case 16: case 31:
      return kTemporalExecutionAttributed | kTemporalSceneGeometry;
    case 1: case 2: case 3: case 9: case 10: case 13: case 17: case 19:
    case 20: case 21: case 23: case 24: case 32: case 34: case 35: case 36:
      return kTemporalExecutionAttributed | kTemporalScreenSpace;
    default:
      return kTemporalExecutionAttributed;
  }
}
inline constexpr uint32_t kTemporalJitterApplied = 1u << 0;
inline constexpr uint32_t kTemporalCameraCut = 1u << 1;
inline constexpr uint32_t kTemporalDepthReversed = 1u << 2;
struct TemporalCommand {
  CommandHeader header{sizeof(TemporalCommand), CommandType::kTemporalUpdate};
  TemporalEvent event = TemporalEvent::kInstance;
  uint32_t device = 0, view = 0, width = 0, height = 0, output_width = 0, output_height = 0,
           flags = 0;
  // BeginScene: actual timestamp. Instance/BeforePostFx/AfterPostFx with
  // CompositeExecution: synchronous lighting occurrence, matching that scope.
  uint64_t sequence = 0, epoch = 0, time_ns = 0;
  std::array<float, 2> jitter{};
  float near_plane = 0.1f, far_plane = 1000.0f, field_of_view = 60, aspect = 1;
  uint64_t instance = 0, drawable = 0, pose = 0;
  std::array<float, 16> projection{}, view_inverse{};
};
constexpr bool SameTemporalScene(const TemporalCommand& a,const TemporalCommand& b) {
  return a.device==b.device&&a.view==b.view&&a.sequence==b.sequence&&
         a.width==b.width&&a.height==b.height&&
         a.output_width==b.output_width&&a.output_height==b.output_height;
}
static_assert(std::is_trivially_copyable_v<TemporalCommand>);
}  // namespace rex::graphics::gta4_native
