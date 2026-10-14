#include "ui/metal/guest_output_context.h"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <thread>
namespace {
size_t checks = 0;
void Check(bool x, const char *s) {
  ++checks;
  if (!x)
    throw std::runtime_error(s);
}
void Fill(id<MTLCommandBuffer> cb, id<MTLTexture> image, double red) {
  auto p = [MTLRenderPassDescriptor renderPassDescriptor];
  p.colorAttachments[0].texture = image;
  p.colorAttachments[0].loadAction = MTLLoadActionClear;
  p.colorAttachments[0].storeAction = MTLStoreActionStore;
  p.colorAttachments[0].clearColor = MTLClearColorMake(red, 0, 0, 1);
  auto e = [cb renderCommandEncoderWithDescriptor:p];
  Check(e != nil, "queue texture fill");
  [e endEncoding];
}
void Copy(id<MTLCommandBuffer> cb, id<MTLTexture> image, id<MTLBuffer> buffer) {
  auto e = [cb blitCommandEncoder];
  Check(e != nil, "queue copy");
  [e copyFromTexture:image
                   sourceSlice:0
                   sourceLevel:0
                  sourceOrigin:MTLOriginMake(0, 0, 0)
                    sourceSize:MTLSizeMake(16, 16, 1)
                      toBuffer:buffer
             destinationOffset:0
        destinationBytesPerRow:256
      destinationBytesPerImage:4096];
  [e endEncoding];
}
bool Wait(id<MTLSharedEvent> e, uint64_t n) {
  const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  for (; std::chrono::steady_clock::now() < until;) {
    if (e.signaledValue >= n)
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}
} // namespace
int main() {
  @autoreleasepool {
    try {
      auto device = MTLCreateSystemDefaultDevice();
      auto render = [device newCommandQueue],
           display = [device newCommandQueue];
      auto ready = [device newEvent], read_done = [device newEvent];
      auto allow_display_finish = [device newSharedEvent],
           read_observed = [device newSharedEvent],
           scene_progress = [device newSharedEvent];
      auto d = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                       width:16
                                      height:16
                                   mipmapped:NO];
      d.storageMode = MTLStorageModePrivate;
      d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
      auto image = [device newTextureWithDescriptor:d];
      auto first_read =
               [device newBufferWithLength:4096
                                   options:MTLResourceStorageModeShared],
           second_read =
               [device newBufferWithLength:4096
                                   options:MTLResourceStorageModeShared];
      bool is8 = false;
      std::shared_ptr<rex::ui::metal::MetalGeneratedFrame> pair;
      rex::ui::metal::MetalGuestOutputRefreshContext first(
          is8, image, 1, &pair, ready, 1, read_done, 0);
      auto r1 = [render commandBuffer];
      first.BeginWrite(r1);
      Fill(r1, image, 0.25);
      first.EndWrite(r1);
      Check(first.synchronization_encoded(), "first mailbox omitted signal");
      [r1 commit];
      auto p1 = [display commandBuffer];
      [p1 encodeWaitForEvent:ready value:1];
      Copy(p1, image, first_read);
      [p1 encodeSignalEvent:read_observed value:1];
      [p1 encodeWaitForEvent:allow_display_finish value:1];
      [p1 encodeSignalEvent:read_done value:1];
      [p1 commit];
      rex::ui::metal::MetalGuestOutputRefreshContext second(
          is8, image, 2, &pair, ready, 2, read_done, 1);
      auto r2 = [render commandBuffer];
      [r2 encodeSignalEvent:scene_progress value:1];
      second.BeginWrite(r2);
      Fill(r2, image, 0.75);
      second.EndWrite(r2);
      [r2 commit];
      const bool had_read = Wait(read_observed, 1),
                 had_progress = Wait(scene_progress, 1);
      // Always release the test-only hold before asserting so failure cannot
      // hang a device queue during test teardown.
      allow_display_finish.signaledValue = 1;
      Check(had_read, "display queue could not read the published first frame");
      Check(had_progress,
            "next scene could not progress independently from presentation");
      auto p2 = [display commandBuffer];
      [p2 encodeWaitForEvent:ready value:2];
      Copy(p2, image, second_read);
      [p2 commit];
      [r1 waitUntilCompleted];
      [r2 waitUntilCompleted];
      [p1 waitUntilCompleted];
      [p2 waitUntilCompleted];
      for (auto cb : {r1, r2, p1, p2})
        Check(cb.status == MTLCommandBufferStatusCompleted,
              "cross-queue GPU command failed");
      for (size_t y = 0; y < 16; ++y)
        for (size_t x = 0; x < 16; ++x) {
          Check(static_cast<const uint8_t *>(
                    first_read.contents)[y * 256 + x * 4] == 64,
                "later producer overwrote an in-flight mailbox read");
          Check(static_cast<const uint8_t *>(
                    second_read.contents)[y * 256 + x * 4] == 191,
                "display read before the second publication was ready");
        }
      Check(second.synchronization_encoded(), "second mailbox omitted signal");
      std::printf("metal_temporal_cross_queue=passed checks=%zu\n", checks);
      return 0;
    } catch (const std::exception &e) {
      std::fprintf(stderr, "Temporal queue test: %s\n", e.what());
      return 1;
    }
  }
}
