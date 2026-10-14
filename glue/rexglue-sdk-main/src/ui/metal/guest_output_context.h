#pragma once

#import <Metal/Metal.h>
#include <rex/ui/presenter.h>

namespace rex::ui::metal {

struct MetalGeneratedFrame {
  id<MTLTexture> generated=nil,real=nil;
  std::shared_ptr<void> lease;
  uint64_t epoch=0,ready_value=0,interval_ns=0;
};

// The producer submits writes before returning success. Generated-frame
// presentation uses explicit GPU events; ordinary rendering keeps one queue.
// The common presenter owns mailbox publication.
class MetalGuestOutputRefreshContext final : public Presenter::GuestOutputRefreshContext {
 public:
  MetalGuestOutputRefreshContext(bool& is_8bpc, id<MTLTexture> texture, uint64_t version, std::shared_ptr<MetalGeneratedFrame>* pair=nullptr,
      id<MTLEvent> ready=nil,uint64_t ready_value=0,id<MTLEvent> read_done=nil,uint64_t read_value=0)
      : GuestOutputRefreshContext(is_8bpc), texture_(texture), version_(version), pair_(pair),
        ready_(ready),read_done_(read_done),ready_value_(ready_value),read_value_(read_value) {}
  id<MTLTexture> texture() const { return texture_; }
  uint64_t image_version() const { return version_; }
  void SetGeneratedFrame(id<MTLTexture> generated,id<MTLTexture> real,std::shared_ptr<void> lease,uint64_t epoch,uint64_t interval_ns=0){
    if(pair_&&generated&&real&&lease)*pair_=std::make_shared<MetalGeneratedFrame>(MetalGeneratedFrame{generated,real,std::move(lease),epoch,ready_value_,interval_ns});
  }

  // The producer inserts these at the mailbox-copy boundary, after scene work.
  // They are no-ops on the normal one-queue path.
  void BeginWrite(id<MTLCommandBuffer> commands){
    if(read_done_&&read_value_)[commands encodeWaitForEvent:read_done_ value:read_value_];
  }
  void EndWrite(id<MTLCommandBuffer> commands){
    if(ready_)[commands encodeSignalEvent:ready_ value:ready_value_];
    synchronized_=true;
  }
  bool synchronization_encoded() const{return !ready_||synchronized_;}
 private:
  id<MTLTexture> texture_ = nil;
  uint64_t version_ = 0;
  std::shared_ptr<MetalGeneratedFrame>* pair_=nullptr;
  id<MTLEvent> ready_=nil,read_done_=nil;
  uint64_t ready_value_=0,read_value_=0;bool synchronized_=false;
};

}  // namespace rex::ui::metal
