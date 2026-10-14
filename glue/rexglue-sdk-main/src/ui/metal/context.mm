#include "context.h"

#include <dispatch/dispatch.h>

#include "metal_ui_library.h"

namespace rex::ui::metal {

std::string MetalError(NSError* error, const char* fallback) {
  const char* description = error.localizedDescription.UTF8String;
  return description ? description : fallback;
}

std::shared_ptr<MetalContext> MetalContext::Create(std::string& error) {
  @autoreleasepool {
    error.clear();
    auto context = std::make_shared<MetalContext>();
    context->device = MTLCreateSystemDefaultDevice();
    if (!context->device) {
      error = "No Metal device is available.";
      return {};
    }
    // Initial support is Apple-silicon macOS. Other renderer choices are unchanged.
    if (!context->device.hasUnifiedMemory || ![context->device supportsFamily:MTLGPUFamilyApple1]) {
      error = "The experimental Metal backend currently requires an Apple GPU.";
      return {};
    }
    context->queue = [context->device newCommandQueue];
    if (!context->queue) {
      error = "Metal command queue creation failed.";
      return {};
    }
    context->queue.label = @"Liberty Metal";
    auto data = dispatch_data_create(kMetalUiLibrary, sizeof(kMetalUiLibrary),
                                     nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    NSError* library_error = nil;
    context->ui_library = [context->device newLibraryWithData:data error:&library_error];
    if (!context->ui_library) {
      error = MetalError(library_error, "Metal UI library loading failed.");
      return {};
    }
    context->ui_library.label = @"Liberty Metal UI";
    return context;
  }
}

}  // namespace rex::ui::metal
