#include "frame_ring.h"

#include "context.h"

namespace rex::ui::metal {

FrameRing::~FrameRing() {
  std::string ignored;
  WaitIdle(ignored);
}

bool FrameRing::Owns(const Slot& candidate) const {
  for (const auto& slot : slots_) if (&slot == &candidate) return true;
  return false;
}

FrameRing::Slot* FrameRing::TryBegin(std::string& error) {
  error.clear();
  for (size_t index = 0; index < active_slot_count_; ++index) {
    const size_t selected = (next_ + index) % active_slot_count_;
    auto& slot = slots_[selected];
    if (slot.encoding) continue;
    if (slot.submission) {
      const auto status = slot.submission.status;
      if (status == MTLCommandBufferStatusError) {
        error = MetalError(slot.submission.error, "Metal submission failed.");
        return nullptr;
      }
      if (status != MTLCommandBufferStatusCompleted) continue;
      slot.submission = nil;
    }
    slot.uploads.Reset();
    slot.encoding = true;
    next_ = (selected + 1) % active_slot_count_;
    return &slot;
  }
  return nullptr;
}

FrameRing::Slot* FrameRing::Begin(std::string& error) {
  if (auto* slot = TryBegin(error)) return slot;
  if (!error.empty()) return nullptr;
  for (size_t i = 0; i < active_slot_count_; ++i) {
    auto& slot = slots_[(next_ + i) % active_slot_count_];
    if (!slot.submission) continue;
    [slot.submission waitUntilCompleted];
    return TryBegin(error);
  }
  error = "All Metal frame slots are already being encoded";
  return nullptr;
}

bool FrameRing::Commit(Slot& slot, id<MTLCommandBuffer> command_buffer) {
  if (!Owns(slot) || !slot.encoding || slot.submission || !command_buffer ||
      command_buffer.status != MTLCommandBufferStatusNotEnqueued ||
      !command_buffer.retainedReferences) return false;
  slot.submission = command_buffer;
  slot.encoding = false;
  [command_buffer commit];
  return true;
}

void FrameRing::Cancel(Slot& slot) {
  if (Owns(slot) && !slot.submission) slot.encoding = false;
}

bool FrameRing::WaitIdle(std::string& error) {
  error.clear();
  for (auto& slot : slots_) {
    if (!slot.submission) continue;
    [slot.submission waitUntilCompleted];
    if (slot.submission.status == MTLCommandBufferStatusError && error.empty())
      error = MetalError(slot.submission.error, "Metal submission failed.");
    slot.submission = nil;
  }
  return error.empty();
}

size_t FrameRing::reserved_bytes() const {
  size_t bytes = 0;
  for (const auto& slot : slots_) bytes += slot.uploads.reserved_bytes();
  return bytes;
}

}  // namespace rex::ui::metal
