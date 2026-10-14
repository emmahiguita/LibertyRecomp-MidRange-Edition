#include "motion_coverage.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace rex::graphics::gta4_metal::temporal {

bool MotionCoverage::Encode(id<MTLCommandBuffer> commands, const Inputs& in,
                            id<MTLLibrary> library, std::string& error) {
  Invalidate();
  error.clear();
  if (!commands || !library || library.device != commands.device ||
      commands.status != MTLCommandBufferStatusNotEnqueued || !commands.retainedReferences) {
    error = "motion coverage requires a recording command buffer and matching library";
    return false;
  }
  const auto matches = [&](id<MTLTexture> image, MTLPixelFormat format) {
    return image && image.device == commands.device &&
           image.textureType == MTLTextureType2D && image.sampleCount == 1 &&
           image.pixelFormat == format && image.width && image.height &&
           image.width == in.motion.width && image.height == in.motion.height &&
           (image.usage & MTLTextureUsageShaderRead) &&
           image.hazardTrackingMode != MTLHazardTrackingModeUntracked;
  };
  if (!matches(in.motion, MTLPixelFormatRG16Float) ||
      !matches(in.reactive, MTLPixelFormatR8Unorm) ||
      !matches(in.depth, MTLPixelFormatR32Float) ||
      !matches(in.previous_depth, MTLPixelFormatR32Float)) {
    error = "motion coverage input format, extent, usage, or tracking is invalid";
    return false;
  }
  if (!pipeline_ || library_ != library) {
    auto function = [library newFunctionWithName:@"liberty_temporal_motion_coverage"];
    NSError* native_error = nil;
    auto pipeline = function ? [commands.device newComputePipelineStateWithFunction:function
                                                                            error:&native_error]
                             : nil;
    if (!pipeline) {
      error = native_error ? native_error.localizedDescription.UTF8String
                           : "motion coverage reduction shader is absent";
      return false;
    }
    library_ = library;
    pipeline_ = pipeline;
  }
  if (!pipeline_.maxTotalThreadsPerThreadgroup) {
    error = "motion coverage pipeline has no executable threadgroup size";
    return false;
  }
  // Completion permits reuse. An abandoned or still-running recording gets a
  // fresh result instead; its retained references protect its original storage.
  const bool reusable = result_ && result_.device == commands.device &&
      (!owner_ || owner_.status == MTLCommandBufferStatusCompleted ||
       owner_.status == MTLCommandBufferStatusError);
  if (!reusable) {
    result_ = [commands.device newBufferWithLength:sizeof(uint32_t)
                                          options:MTLResourceStorageModeShared];
    if (!result_) {
      error = "motion coverage result allocation failed";
      return false;
    }
    result_.label = @"Liberty current-frame motion coverage";
  }
  const uint32_t clear = 0;
  std::memcpy(result_.contents, &clear, sizeof(clear));
  auto encoder = [commands computeCommandEncoder];
  if (!encoder) {
    error = "motion coverage reduction encoder failed";
    return false;
  }
  encoder.label = @"Liberty frame-generation pixel coverage validation";
  [encoder setComputePipelineState:pipeline_];
  [encoder setTexture:in.reactive atIndex:0];
  [encoder setTexture:in.motion atIndex:1];
  [encoder setTexture:in.depth atIndex:2];
  [encoder setTexture:in.previous_depth atIndex:3];
  [encoder setBuffer:result_ offset:0 atIndex:0];
  const NSUInteger x = std::min<NSUInteger>(16, pipeline_.maxTotalThreadsPerThreadgroup);
  const NSUInteger y = std::min<NSUInteger>(16, pipeline_.maxTotalThreadsPerThreadgroup / x);
  // Full groups ensure every lane reaches both threadgroup barriers, including
  // the padded lanes of odd-sized input images.
  [encoder dispatchThreadgroups:MTLSizeMake((in.motion.width + x - 1) / x,
                                           (in.motion.height + y - 1) / y, 1)
          threadsPerThreadgroup:MTLSizeMake(x, y, 1)];
  [encoder endEncoding];
  owner_ = commands;
  encoded_ = true;
  return true;
}

bool MotionCoverage::Complete() const {
  if (!encoded_ || !result_ || !owner_ || owner_.status != MTLCommandBufferStatusCompleted)
    return false;
  uint32_t invalid = 0;
  std::memcpy(&invalid, result_.contents, sizeof(invalid));
  return invalid == 0;
}

}  // namespace rex::graphics::gta4_metal::temporal
