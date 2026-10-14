#pragma once
#import <Metal/Metal.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace rex::ui::metal {
struct MetalContext;

// Exact layout of the existing hdr_present_ps.glsl push-constant block.
// The final padding is part of MSL's structure alignment.
struct alignas(16) OutputTransferConstants {
  int32_t source_extent[2]{};
  int32_t destination_extent[2]{};
  float hdr_headroom = 1;
  uint32_t output_mode = 4;  // Original frontbuffer is perceptual sRGB.
  uint32_t hdr_mode = 0;
  float paper_white_nits = 203;
  float peak_nits = 400;
  float shoulder_start = 0;
  float shoulder_power = 2.5f;
  uint32_t padding = 0;
};
static_assert(sizeof(OutputTransferConstants) == 48);

// Reuses the authored output shader for SDR/FXAA/SSAA and HDR. No shader math
// is duplicated here. The caller owns the render pass and destination lifetime.
class OutputTransfer {
 public:
  bool Initialize(std::shared_ptr<MetalContext> context, std::string& error);
  bool Draw(id<MTLRenderCommandEncoder> encoder, id<MTLTexture> source,
            id<MTLTexture> destination, OutputTransferConstants constants,
            std::string& error) const;
  // Only the final bilinear + HDR pair is fused; AA, CAS and FSR keep their
  // original ordering. Viewport comes from the shared frontend flow planner.
  bool DrawBilinearHdr(id<MTLRenderCommandEncoder> encoder,id<MTLTexture> source,
      id<MTLTexture> destination,MTLViewport viewport,OutputTransferConstants constants,std::string& error) const;

 private:
  std::array<id<MTLRenderPipelineState>, 2> pipelines_{};
  std::array<id<MTLRenderPipelineState>,2> bilinear_hdr_{};
  id<MTLSamplerState> sampler_ = nil;
};
}  // namespace rex::ui::metal
