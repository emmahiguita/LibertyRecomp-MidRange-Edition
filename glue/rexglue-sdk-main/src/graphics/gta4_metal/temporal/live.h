#pragma once
#include <rex/graphics/gta4_native/temporal_commands.h>
#include "../../../ui/metal/context.h"
#include "../resources.h"
#include "effects.h"
#include "geometry_history.h"
#include "motion_coverage.h"
namespace rex::graphics::gta4_metal::temporal {
struct DrawParameters {
  std::array<float, 2> input_extent{}, jitter_clip{}, previous_jitter_clip{};
  uint32_t valid_history = 0, reactive = 0, ui_mode = 0, padding[3]{};
};
static_assert(sizeof(DrawParameters) == 48);
class Live {
 public:
  bool Initialize(std::shared_ptr<ui::metal::MetalContext>, std::string&);
  id<MTLFunction> DepthMotionFunction() const { return depth_motion_fragment_; }
  bool Begin(id<MTLCommandBuffer>, const gta4_native::TemporalCommand&, Method, uint32_t, uint32_t,
             bool, std::string&);
  bool CaptureDepth(id<MTLCommandBuffer>, std::string&);
  bool SetExposure(id<MTLCommandBuffer>, id<MTLTexture> adaptation, float exposure,
                   float tone_scale, std::string&);
  id<MTLTexture> Resolve(id<MTLCommandBuffer>, id<MTLTexture>, std::string&);
  bool AfterComposite(id<MTLCommandBuffer>, id<MTLTexture>, std::string&);
  bool RecoverComposite(id<MTLCommandBuffer>, const std::shared_ptr<SurfaceResource>&,
                        std::string&);
  bool StartUi(id<MTLCommandBuffer>, std::string&);
  bool CopyColor(id<MTLCommandBuffer>, id<MTLTexture>, id<MTLTexture>, std::string&,
                 std::array<float, 2> offset = {});
  bool Compose(id<MTLCommandBuffer>, id<MTLTexture>, id<MTLTexture>, std::string&);
  bool Generate(id<MTLCommandBuffer>, InterpolationOutput&, std::string&);
  bool NeedsMotionCoverage() const;
  bool ValidateMotionCoverage(id<MTLCommandBuffer>, std::string&);
  id<MTLTexture> ResampleHalf(id<MTLCommandBuffer>, id<MTLTexture>, std::string&);
  bool ComposeInPlace(id<MTLCommandBuffer>, id<MTLTexture>, std::string&);
  std::shared_ptr<SurfaceResource> HighComposite(MTLPixelFormat, std::string&);
  std::shared_ptr<SurfaceResource> FilterComposite(std::string&);
  void End();
  void Reset();
  bool AdoptExecutionCamera();
  bool IsExecutionCamera() const;
  // A spatial fallback is a current scene image, but never temporal history or
  // an input to frame generation.
  bool HasSceneOutput() const { return resolved || spatial_fallback; }
  bool TemporalHistoryReset() const { return resolved && resolved_.history_reset; }
  bool active = false, ui_active = false, resolved = false, depth_captured = false,
       frame_generation = false, ui_exact = true, spatial_fallback = false,
       composite_seen = false;
  bool main_view = false, execution_camera_valid = false, adopted_camera = false;
  bool execution_scene_geometry = false, execution_screen_space = false,
       execution_attributed = false;
  bool execution_composite = false, execution_final_composite = false;
  uint64_t execution_composite_scope = 0, active_composite_scope = 0;
  // Draw execution may only invalidate this assertion. Generate additionally
  // requires actual world draws and matching prior-history draw coverage.
  bool motion_coverage_complete = true;
  bool jitter_decided = false, jitter_eligible = false;
  std::array<float, 16> execution_projection{}, execution_inverse{}, actual_projection{},
      actual_inverse{};
  std::array<float, 2> camera_half_pixel{};
  uint64_t instance = 0, drawable = 0, pose = 0, world_draws = 0, history_draws = 0,
           reactive_draws = 0, ui_draws = 0, jittered_draws = 0;
  std::string fallback_reason, generation_skip_reason;
  uint64_t composite_shader = 0, filtered_composites = 0;
  uint32_t composite_scene_stage = 0;
  Frame metadata{};
  Configuration config{};
  Inputs inputs{};
  id<MTLTexture> ui_add = nil, ui_transmit = nil, final_scene = nil;
  std::shared_ptr<SurfaceResource> depth_source, composite_source, executed_composite_source;
  GeometryHistory geometry;
  uint64_t last_depth_serial = 0, presentation_interval_ns = 0, observed_view = 0,
           observed_sequence = 0;
  uint32_t diagnostic_depth_draws = 0;
  uint32_t diagnostic_missing_motion = 0;

 private:
  std::shared_ptr<ui::metal::MetalContext> context_;
  id<MTLLibrary> library_ = nil;
  id<MTLFunction> depth_motion_fragment_ = nil;
  id<MTLComputePipelineState> depth_export_ = nil, color_copy_ = nil, compose_ = nil,
                              compose_inplace_ = nil, exposure_hint_ = nil, background_motion_ = nil;
  id<MTLTexture> converted_ = nil, half_scene_ = nil, spatial_output_ = nil;
  id<MTLTexture> exposure_ = nil;
  id<MTLTexture> recovered_composite_ = nil;
  std::shared_ptr<SurfaceResource> high_composite_, filtered_composite_;
  AntiAliasing aa_;
  FrameInterpolation interpolation_;
  MotionCoverage motion_coverage_;
  Output resolved_;
  bool previous_camera_valid_ = false, composite_ready_ = false;
  std::array<float, 16> previous_camera_{};
  std::array<float, 16> previous_projection_{}, background_transform_{};
  History camera_history_;
  bool background_history_valid_ = false;
  uint64_t last_time_ns_ = 0, last_epoch_ = 0, last_view_ = 0, smoothed_interval_ns_ = 0;
  uint64_t uncovered_frames_ = 0;
  bool CreateImage(__strong id<MTLTexture>&, MTLPixelFormat, uint32_t, uint32_t, NSString*,
                   std::string&);
  bool Clear(id<MTLCommandBuffer>, id<MTLTexture>, double, std::string&);
  void ResetHistory();
  bool CompleteBackgroundMotion(id<MTLCommandBuffer>, std::string&);
};
}  // namespace rex::graphics::gta4_metal::temporal
