#pragma once

#include "effects.h"

namespace rex::graphics::gta4_metal::temporal {

// Per-pixel geometric admission, independent of the AA reactive color mask.
// Missing visible geometry is represented by invalid expected previous depth.
// The caller owns submission and synchronization; this class never waits.
// Serialized by the renderer, like Live and the temporal effect instances.
class MotionCoverage {
 public:
  MotionCoverage() = default;
  MotionCoverage(const MotionCoverage&) = delete;
  MotionCoverage& operator=(const MotionCoverage&) = delete;

  bool Encode(id<MTLCommandBuffer>, const Inputs&, id<MTLLibrary>, std::string&);
  // True only after this encode completed successfully and every pixel passed.
  // Invalidate at each new scene; a previous frame is never evidence for it.
  bool Complete() const;
  void Invalidate() { encoded_ = false; }

 private:
  id<MTLLibrary> library_ = nil;
  id<MTLComputePipelineState> pipeline_ = nil;
  id<MTLBuffer> result_ = nil;
  id<MTLCommandBuffer> owner_ = nil;
  bool encoded_ = false;
};

}  // namespace rex::graphics::gta4_metal::temporal
