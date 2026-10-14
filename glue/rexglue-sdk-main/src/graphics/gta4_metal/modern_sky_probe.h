#pragma once
#import <Metal/Metal.h>
#include <array>
#include <memory>
#include <string>

namespace rex::graphics::gta4_metal {
// Caller ends render encoding before Start/Finish. No waits and no writes to
// title resources. The completion block owns its buffers and context by value.
class ModernSkyProbe {
 public:
  static constexpr uint32_t kColumns = 32, kRows = 18, kSamples = kColumns * kRows;
  struct Capture {
    id<MTLBuffer> before = nil, after = nil, visibility = nil;
    std::string context;
    bool queried = false;
  };
  struct Statistics {
    uint64_t visible_samples = 0;
    uint32_t changed = 0, black = 0, invalid_before = 0, invalid_after = 0, negative = 0;
    float minimum = 0, maximum = 0;
    std::array<float, 4> top_center{};
  };
  std::shared_ptr<Capture> Start(id<MTLDevice>, id<MTLCommandBuffer>, id<MTLTexture>,
      std::string context, std::string& error);
  bool Finish(id<MTLCommandBuffer>, id<MTLTexture>, std::shared_ptr<Capture>, std::string& error);
  static Statistics Read(const Capture&);
 private:
  bool Sample(id<MTLCommandBuffer>, id<MTLTexture>, id<MTLBuffer>, std::string&);
  id<MTLComputePipelineState> single_ = nil, multisample_ = nil;
};
}  // namespace rex::graphics::gta4_metal
