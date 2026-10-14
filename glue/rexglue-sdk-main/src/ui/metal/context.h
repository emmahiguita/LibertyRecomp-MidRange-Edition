#pragma once

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <memory>
#include <string>

namespace rex::ui::metal {

inline constexpr MTLPixelFormat kPresentationFormat = MTLPixelFormatBGRA8Unorm;

// One device and one queue, shared by presentation and the selected renderer.
// No Vulkan objects, global singleton, heap allocator, or residency registry.
struct MetalContext {
  static std::shared_ptr<MetalContext> Create(std::string& error);
  id<MTLDevice> device = nil;
  id<MTLCommandQueue> queue = nil;
  id<MTLLibrary> ui_library = nil;
  id<MTLLibrary> pass_library = nil;
};

std::string MetalError(NSError* error, const char* fallback);

}  // namespace rex::ui::metal
