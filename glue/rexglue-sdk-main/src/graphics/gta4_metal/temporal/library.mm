#import <Foundation/Foundation.h>
#include "effects.h"
#include "metal_temporal_library.h"
namespace rex::graphics::gta4_metal::temporal {
id<MTLLibrary> CreateLibrary(id<MTLDevice> device, std::string& error) {
  error.clear();
  if (!device) {
    error = "temporal shader library needs a Metal device";
    return nil;
  }
  auto data = dispatch_data_create(kMetalTemporalLibrary, sizeof(kMetalTemporalLibrary), nullptr,
                                   DISPATCH_DATA_DESTRUCTOR_DEFAULT);
  NSError* native_error = nil;
  auto library = [device newLibraryWithData:data error:&native_error];
  if (!library)
    error = native_error ? native_error.localizedDescription.UTF8String
                         : "temporal library creation failed";
  return library;
}
}  // namespace rex::graphics::gta4_metal::temporal
