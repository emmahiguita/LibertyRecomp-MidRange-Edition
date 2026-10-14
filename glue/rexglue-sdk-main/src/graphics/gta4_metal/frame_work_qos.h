#pragma once
#include <pthread.h>
#include <pthread/qos.h>
#include <cstdint>
#include <utility>
namespace rex::graphics::gta4_metal {
// A displayed image depends on this bounded recording. An override expresses
// that dependency without changing guest-visible priority or opting the thread
// out of QoS. Release it at commit, not after presentation/back-pressure waits.
// The serialized renderer owns this object; the override may be ended on shutdown.
class FrameWorkQos {
 public:
  FrameWorkQos() = default;
  FrameWorkQos(const FrameWorkQos&) = delete;
  FrameWorkQos& operator=(const FrameWorkQos&) = delete;
  ~FrameWorkQos() { End(); }
  bool Begin(bool enabled) {
    if (!enabled) { End(); return false; }
    if (override_) return true;
    override_ = pthread_override_qos_class_start_np(pthread_self(), QOS_CLASS_USER_INITIATED, 0);
    if (override_) ++begun_; else ++failed_;
    return override_ != nullptr;
  }
  void End() noexcept {
    if (auto value = std::exchange(override_, nullptr)) {
      last_end_error_ = pthread_override_qos_class_end_np(value);
      ++ended_;
    }
  }
  bool active() const { return override_ != nullptr; }
  uint64_t begun() const { return begun_; }
  uint64_t ended() const { return ended_; }
  uint64_t failed() const { return failed_; }
  int last_end_error() const { return last_end_error_; }
 private:
  pthread_override_t override_ = nullptr;
  uint64_t begun_ = 0, ended_ = 0, failed_ = 0;
  int last_end_error_ = 0;
};
}  // namespace rex::graphics::gta4_metal
