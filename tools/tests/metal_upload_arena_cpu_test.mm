#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "ui/metal/frame_ring.h"
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

// Execute the production arena/ring code without a GPU. These objects implement
// only the Metal methods those classes consume, and count metadata queries.
@interface ArenaTestBuffer : NSObject {
 @public
  std::vector<std::byte> bytes;
  NSUInteger mapping_queries, length_queries, address_queries;
  BOOL unmapped;
}
@property(copy) NSString* label;
- (void*)contents;
- (NSUInteger)length;
- (uint64_t)gpuAddress;
@end
@implementation ArenaTestBuffer
- (void*)contents { ++mapping_queries; return unmapped ? nullptr : bytes.data(); }
- (NSUInteger)length { ++length_queries; return bytes.size(); }
- (uint64_t)gpuAddress {++address_queries;return reinterpret_cast<uintptr_t>(bytes.data());}
@end

@interface ArenaTestDevice : NSObject
@property(strong) NSMutableArray<ArenaTestBuffer*>* buffers;
@property BOOL fail_allocation;
@property BOOL fail_mapping;
- (id<MTLBuffer>)newBufferWithLength:(NSUInteger)length options:(MTLResourceOptions)options;
@end
@implementation ArenaTestDevice
- (instancetype)init {
  if ((self = [super init])) self.buffers = [NSMutableArray new];
  return self;
}
- (id<MTLBuffer>)newBufferWithLength:(NSUInteger)length options:(MTLResourceOptions)options {
  if (self.fail_allocation) { self.fail_allocation = NO; return nil; }
  auto buffer = [ArenaTestBuffer new]; buffer->bytes.resize(length);
  buffer->unmapped = self.fail_mapping; self.fail_mapping = NO;
  [self.buffers addObject:buffer];
  return (id<MTLBuffer>)buffer;
}
@end

@interface ArenaTestCommands : NSObject
@property MTLCommandBufferStatus status;
- (BOOL)retainedReferences;
- (NSError*)error;
- (void)commit;
- (void)waitUntilCompleted;
@end
@implementation ArenaTestCommands
- (BOOL)retainedReferences { return YES; }
- (NSError*)error { return nil; }
- (void)commit { self.status = MTLCommandBufferStatusCommitted; }
- (void)waitUntilCompleted { self.status = MTLCommandBufferStatusCompleted; }
@end

namespace rex::ui::metal {
std::string MetalError(NSError*, const char* fallback) { return fallback; }
}
static size_t checks;
static void Check(bool value, const char* message) {
  ++checks; if (!value) throw std::runtime_error(message);
}

int main() {
 @autoreleasepool {
  try {
    using namespace rex::ui::metal;
    auto fake = [ArenaTestDevice new]; auto device = (id<MTLDevice>)fake;
    {
      UploadArena arena(131072);
      auto first = arena.Allocate(device,65520,16);
      auto second = arena.Allocate(device,64,16);
      auto rest = arena.Allocate(device,65472,16);
      auto leftover = arena.Allocate(device,16,16);
      Check(first && second && rest && leftover,"Mixed-size allocation failed");
      Check(first.buffer != second.buffer && second.buffer == rest.buffer,"Slab selection failed");
      Check(leftover.buffer == first.buffer && leftover.offset == 65520,"Earlier slab tail was stranded");
      Check(first.gpu_address==reinterpret_cast<uintptr_t>(first.data),"Cached GPU address is not the slab base");
      Check(leftover.gpu_address==first.gpu_address+leftover.offset,"Cached GPU address lost the slice offset");
      Check(arena.reserved_bytes() == 131072 && arena.allocation_count() == 2,"Reserved budget changed");
      Check(!arena.Allocate(device,1,1),"Full arena exceeded budget");
      std::memset(first.data,0x35,65520); std::memset(second.data,0xA7,64);
      std::memset(rest.data,0x93,65472); std::memset(leftover.data,0xF1,16);
      auto* a = static_cast<const unsigned char*>(first.data);
      auto* b = static_cast<const unsigned char*>(second.data);
      for(size_t i=0;i<65536;++i) {
        Check(a[i] == (i<65520?0x35:0xF1),"First slab allocations overlapped");
        Check(b[i] == (i<64?0xA7:0x93),"Second slab allocations overlapped");
      }
      arena.Reset();
      auto reset = arena.Allocate(device,65536,4096);
      Check(reset && reset.buffer == first.buffer && reset.offset == 0,"Reset did not reuse original slab");
      Check(arena.allocation_count() == 2,"Reset allocated new storage");
    }
    {
      UploadArena arena(65536);
      auto a = arena.Allocate(device,3,1);
      auto b = arena.Allocate(device,3,64);
      auto c = arena.Allocate(device,257,256);
      Check(a && b && c && a.offset==0 && b.offset==64 && c.offset==256,"Alignment contract changed");
      const auto count=arena.allocation_count(), reserved=arena.reserved_bytes();
      Check(!arena.Allocate(device,0,16),"Zero allocation accepted");
      Check(!arena.Allocate(device,16,0),"Zero alignment accepted");
      Check(!arena.Allocate(device,16,3),"Non-power-of-two alignment accepted");
      Check(!arena.Allocate(device,std::numeric_limits<size_t>::max(),16),"Wrapping size accepted");
      Check(!arena.Allocate(nil,16,16),"Missing device accepted");
      Check(arena.allocation_count()==count && arena.reserved_bytes()==reserved,"Invalid allocation changed arena");
    }
    {
      UploadArena arena(1024);
      fake.fail_allocation=YES; Check(!arena.Allocate(device,16,16),"Device allocation failure ignored");
      fake.fail_mapping=YES; Check(!arena.Allocate(device,16,16),"Mapping failure ignored");
      Check(arena.reserved_bytes()==0 && arena.allocation_count()==0,"Failed allocation consumed budget");
      auto last=arena.Allocate(device,1024,16);
      Check(last && !arena.Allocate(device,1,1),"Partial slab budget not enforced");
    }
    {
      FrameRing ring(65536,2); std::string error;
      auto* first=ring.TryBegin(error); Check(first!=nullptr,"First slot unavailable");
      auto a=first->uploads.Allocate(device,256,16); Check(bool(a),"First frame upload failed");
      std::memset(a.data,0x31,256);
      auto first_commands=[ArenaTestCommands new];
      Check(ring.Commit(*first,(id<MTLCommandBuffer>)first_commands),"First commit rejected");
      auto* second=ring.TryBegin(error); Check(second && second!=first,"In-flight slot reused");
      auto b=second->uploads.Allocate(device,256,16); Check(b && a.buffer!=b.buffer,"In-flight buffer aliased");
      std::memset(b.data,0x72,256);
      auto second_commands=[ArenaTestCommands new];
      Check(ring.Commit(*second,(id<MTLCommandBuffer>)second_commands),"Second commit rejected");
      Check(!ring.TryBegin(error) && error.empty(),"Pending frames were overwritten");
      for(size_t i=0;i<256;++i) Check(static_cast<unsigned char*>(a.data)[i]==0x31,"Pending upload bytes changed");
      first_commands.status=MTLCommandBufferStatusCompleted;
      auto* reused=ring.TryBegin(error); Check(reused==first,"Completed slot not reused");
      auto c=reused->uploads.Allocate(device,256,16);
      Check(c && c.buffer==a.buffer && c.offset==0,"Completed arena was not reset");
      std::memset(c.data,0xB4,256);
      for(size_t i=0;i<256;++i) Check(static_cast<unsigned char*>(b.data)[i]==0x72,"Other pending frame was mutated");
      ring.Cancel(*reused); second_commands.status=MTLCommandBufferStatusCompleted;
    }
    for(ArenaTestBuffer* buffer in fake.buffers) {
      Check(buffer->length_queries==0,"Slice allocation queried Metal length");
      Check(buffer->mapping_queries==1,"Slice allocation remapped existing storage");
      Check(buffer->address_queries==(buffer->unmapped?0:1),"GPU address was not queried exactly once for a mapped slab");
    }
    std::printf("metal_upload_arena_cpu=passed checks=%zu gpu_calls=0\n",checks);
    return 0;
  } catch(const std::exception& error) {std::fprintf(stderr,"%s\n",error.what());return 1;}
 }
}
