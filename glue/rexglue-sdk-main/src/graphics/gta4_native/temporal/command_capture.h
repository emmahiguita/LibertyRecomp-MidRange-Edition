#pragma once

#include <algorithm>
#include <cmath>
#include <memory>

#include <rex/graphics/gta4_native/temporal_commands.h>

namespace rex::graphics::gta4_native::temporal {

inline bool ValidTemporalCommand(const TemporalCommand& command) {
  constexpr uint32_t known_flags = kTemporalJitterApplied | kTemporalCameraCut |
      kTemporalDepthReversed | kTemporalMainView | kTemporalExecutionCamera |
      kTemporalSceneGeometry | kTemporalScreenSpace | kTemporalExecutionAttributed |
      kTemporalCompositeExecution | kTemporalFinalCompositeExecution;
  if (command.header.type != CommandType::kTemporalUpdate ||
      command.header.size != sizeof(command) || command.event > TemporalEvent::kEndFrame ||
      !command.device || !command.sequence || (command.flags & ~known_flags) ||
      ((command.flags & kTemporalFinalCompositeExecution) &&
       !(command.flags & kTemporalCompositeExecution))) return false;
  const auto finite = [](const auto& values) {
    return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
  };
  if (!finite(command.projection) || !finite(command.view_inverse) || !finite(command.jitter))
    return false;
  if (command.event == TemporalEvent::kInstance) {
    return !(command.flags & kTemporalExecutionCamera) || command.view != 0;
  }
  return command.view && command.epoch && command.width && command.height &&
      command.output_width >= command.width && command.output_height >= command.height &&
      std::abs(command.jitter[0]) <= 0.5f && std::abs(command.jitter[1]) <= 0.5f &&
      std::isfinite(command.near_plane) && command.near_plane > 0.0f &&
      std::isfinite(command.far_plane) && command.far_plane > command.near_plane &&
      std::isfinite(command.field_of_view) && command.field_of_view > 0.0f &&
      command.field_of_view < 180.0f && std::isfinite(command.aspect) && command.aspect > 0.0f &&
      (command.event != TemporalEvent::kBeginScene || command.time_ns != 0);
}

// A title instance declaration persists across its mesh draws on the submitting
// thread. Each queued draw receives its own immutable shared ownership, so a
// concurrent submitter or later instance declaration cannot change its identity.
class CommandCapture {
 public:
  void Observe(const TemporalCommand& command) {
    if (command.event == TemporalEvent::kInstance) {
      pending_.owner = identity_;
      pending_.command = std::make_shared<const TemporalCommand>(command);
    } else if (command.event == TemporalEvent::kBeginScene ||
               command.event == TemporalEvent::kEndFrame) {
      Reset();
    }
  }
  std::shared_ptr<const TemporalCommand> Capture(uint32_t device) const {
    if (pending_.owner.lock() != identity_ || !pending_.command ||
        pending_.command->device != device) return {};
    return pending_.command;
  }
  void Reset() {
    if (pending_.owner.lock() == identity_) pending_ = {};
  }

 private:
  struct Pending {
    std::weak_ptr<const char> owner;
    std::shared_ptr<const TemporalCommand> command;
  };
  std::shared_ptr<const char> identity_ = std::make_shared<const char>();
  inline static thread_local Pending pending_;
};

}  // namespace rex::graphics::gta4_native::temporal
