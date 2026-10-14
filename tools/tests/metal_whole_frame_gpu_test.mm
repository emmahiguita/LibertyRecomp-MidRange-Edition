#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "ui/metal/frame_ring.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
namespace rex::ui::metal {
std::string MetalError(NSError* e,const char* fallback) {
  const char* text=e.localizedDescription.UTF8String;return text?text:fallback;
}}
static size_t checks;
static void Check(bool v,const char* text) {++checks;if(!v)throw std::runtime_error(text);}
struct ReleaseGate { id<MTLSharedEvent> event; ~ReleaseGate(){event.signaledValue=1;} };
int main() {
 @autoreleasepool {
  try {
   using namespace rex::ui::metal;
   auto device=MTLCreateSystemDefaultDevice();Check(device!=nil,"device");
   auto queue=[device newCommandQueue];Check(queue!=nil,"queue");
   std::string error;
   for (unsigned trial=0;trial<128;++trial) {
    FrameRing ring(65536,2);
    auto event=[device newSharedEvent];Check(event!=nil,"event");
    // Gate is destroyed before the ring even if an assertion throws.
    ReleaseGate release{event};
    auto first=ring.Begin(error);Check(first!=nullptr,"first frame slot");
    auto upload=first->uploads.Allocate(device,256,16);Check(bool(upload),"first upload");
    std::memset(upload.data,int(trial),256);
    auto result=[device newBufferWithLength:1024 options:MTLResourceStorageModeShared];
    Check(result!=nil,"readback");std::memset(result.contents,0xCD,1024);
    auto first_commands=[queue commandBuffer];
    [first_commands encodeWaitForEvent:event value:1];
    auto encoder=[first_commands blitCommandEncoder];
    [encoder copyFromBuffer:upload.buffer sourceOffset:upload.offset toBuffer:result destinationOffset:0 size:256];
    [encoder endEncoding];
    const bool split=trial==0;
    if (!split) {
      // The final image's operation is in the same resource lifetime.
      auto final=[first_commands blitCommandEncoder];
      [final copyFromBuffer:upload.buffer sourceOffset:upload.offset toBuffer:result destinationOffset:256 size:256];
      [final endEncoding];
    }
    Check(ring.Commit(*first,first_commands),"first submit");
    auto second=ring.TryBegin(error);Check(second!=nullptr,"next CPU frame did not overlap gated GPU frame");
    Check(first_commands.status!=MTLCommandBufferStatusCompleted,"gate did not block first frame");
    auto second_commands=[queue commandBuffer];
    if(split) {
      auto final=[second_commands blitCommandEncoder];
      [final copyFromBuffer:upload.buffer sourceOffset:upload.offset toBuffer:result destinationOffset:256 size:256];
      [final endEncoding];
      Check(ring.Commit(*second,second_commands),"split output submit");
      Check(ring.TryBegin(error)==nullptr && error.empty(),"negative control did not fill ring");
    } else {
      auto next=second->uploads.Allocate(device,256,16);Check(bool(next),"second upload");
      Check(next.buffer!=upload.buffer,"in-flight CPU memory aliased");
      std::memset(next.data,int(trial+1),256);
      auto copy=[second_commands blitCommandEncoder];
      [copy copyFromBuffer:next.buffer sourceOffset:next.offset toBuffer:result destinationOffset:512 size:256];
      [copy endEncoding];
      Check(ring.Commit(*second,second_commands),"next full frame submit");
      Check(ring.TryBegin(error)==nullptr && error.empty(),"unbounded third frame admitted");
    }
    event.signaledValue=1;Check(ring.WaitIdle(error),error.c_str());
    auto bytes=static_cast<const unsigned char*>(result.contents);
    for(size_t i=0;i<1024;++i) {
      unsigned char expected=i<512?static_cast<unsigned char>(trial):
          (i<768&&!split)?static_cast<unsigned char>(trial+1):0xCD;
      Check(bytes[i]==expected,"protected upload lifetime or guard bytes changed");
    }
    auto reused=ring.TryBegin(error);Check(reused!=nullptr,"completed frame did not retire");ring.Cancel(*reused);
   }
   std::printf("whole_frame_gpu_pass trials=128 checks=%zu negative_control=blocked complete_frame_overlap=passed device=%s\n",checks,device.name.UTF8String);
   return 0;
  } catch(const std::exception& e){std::fprintf(stderr,"whole frame test: %s\n",e.what());return 1;}
 }
}
