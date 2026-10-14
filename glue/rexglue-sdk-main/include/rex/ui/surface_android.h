#pragma once
/**
 * @file        rex/ui/surface_android.h
 * @brief       Android ANativeWindow surface for Vulkan presentation.
 */

#include <rex/ui/surface.h>

#include <android/native_window.h>

namespace rex {
namespace ui {

class AndroidNativeWindowSurface final : public Surface {
 public:
  // Holds its own reference: SDL releases the ANativeWindow as soon as the
  // Java surface is destroyed (screen off, home), while the presenter may
  // still be using this surface. A dangling pointer here faulted inside
  // RefBase::incStrong, which the guest-memory SIGSEGV handler then retried
  // forever (black screen after unlocking).
  explicit AndroidNativeWindowSurface(ANativeWindow* window) : window_(window) {
    if (window_) ANativeWindow_acquire(window_);
  }
  ~AndroidNativeWindowSurface() override {
    if (window_) ANativeWindow_release(window_);
  }
  AndroidNativeWindowSurface(const AndroidNativeWindowSurface&) = delete;
  AndroidNativeWindowSurface& operator=(const AndroidNativeWindowSurface&) = delete;
  TypeIndex GetType() const override { return kTypeIndex_AndroidNativeWindow; }
  ANativeWindow* window() const { return window_; }

 protected:
  bool GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const override {
    if (!window_) return false;
    width_out = static_cast<uint32_t>(ANativeWindow_getWidth(window_));
    height_out = static_cast<uint32_t>(ANativeWindow_getHeight(window_));
    return true;
  }

 private:
  ANativeWindow* window_;
};

}  // namespace ui
}  // namespace rex
