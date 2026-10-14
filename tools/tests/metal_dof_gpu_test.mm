#include "graphics/gta4_metal/depth_of_field.h"
#include "ui/metal/context.h"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace rex::graphics::gta4_metal;
using namespace rex::graphics::gta4_native;
namespace {
void Check(bool ok, const std::string &why) {
  if (!ok)
    throw std::runtime_error(why);
}
struct Input {
  uint32_t width, height;
  SplitPostFxParameters parameters;
  std::vector<float> scene, half, depth, mask;
};
Input Read(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  Input in{};
  file.read(reinterpret_cast<char *>(&in.width), sizeof(in.width));
  file.read(reinterpret_cast<char *>(&in.height), sizeof(in.height));
  Check(in.width && in.height && in.width <= 8192 && in.height <= 8192,
        "fixture dimensions");
  for (auto *p : {&in.parameters.dof_projection, &in.parameters.dof_distance,
                  &in.parameters.dof_blur})
    file.read(reinterpret_cast<char *>(p->data()), sizeof(*p));
  for (auto *p : {&in.scene, &in.half, &in.depth, &in.mask}) {
    p->resize(p == &in.half ? size_t(in.width / 2) * (in.height / 2) * 4
                            : size_t(in.width) * in.height * 4);
    file.read(reinterpret_cast<char *>(p->data()), p->size() * sizeof(float));
  }
  Check(bool(file), "fixture read");
  return in;
}
} // namespace
int main(int argc, char **argv) {
  @autoreleasepool {
    try {
      Check(argc == 4, "usage: rex-metal-dof-test passes.metallib "
                       "fixture-directory output-directory");
      std::string error;
      auto context = rex::ui::metal::MetalContext::Create(error);
      Check(bool(context), error);
      NSError *native_error = nil;
      context->pass_library = [context->device
          newLibraryWithURL:
              [NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]]]
                      error:&native_error];
      Check(context->pass_library != nil,
            rex::ui::metal::MetalError(native_error, "pass library"));
      DepthOfField dof;
      std::filesystem::create_directories(argv[3]);
      std::vector<std::filesystem::path> cases;
      for (const auto &entry : std::filesystem::directory_iterator(argv[2]))
        if (entry.path().extension() == ".input")
          cases.push_back(entry.path());
      std::sort(cases.begin(), cases.end());
      Check(!cases.empty(), "no fixtures");
      for (const auto &path : cases) {
        auto in = Read(path);
        auto texture = [&](uint32_t w, uint32_t h,
                           const std::vector<float> &values) {
          auto d = [MTLTextureDescriptor
              texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                           width:w
                                          height:h
                                       mipmapped:NO];
          d.storageMode = MTLStorageModeShared;
          d.usage = MTLTextureUsageShaderRead;
          auto image = [context->device newTextureWithDescriptor:d];
          Check(image != nil, "input image");
          [image replaceRegion:MTLRegionMake2D(0, 0, w, h)
                   mipmapLevel:0
                     withBytes:values.data()
                   bytesPerRow:w * 4 * sizeof(float)];
          return image;
        };
        auto scene = texture(in.width, in.height, in.scene),
             half = texture(in.width / 2, in.height / 2, in.half);
        auto depth = texture(in.width, in.height, in.depth),
             mask = texture(in.width, in.height, in.mask);
        auto commands = [context->queue commandBuffer];
        auto invalid = in.parameters;
        invalid.dof_projection[0] = NAN;
        Check(dof.Record(context, commands, scene, half, depth, mask, invalid,
                         error) == nil,
              "invalid DoF must retain stock");
        const bool zero = NativeDofCanBeElided(in.parameters.dof_projection,
                                               in.parameters.dof_distance,
                                               in.parameters.dof_blur);
        if (!zero)
          Check(dof.Record(context, commands, scene, nil, depth, mask,
                           in.parameters, error) == nil,
                "missing half scene must retain stock");
        auto result = dof.Record(context, commands, scene, zero ? nil : half,
                                 depth, mask, in.parameters, error);
        Check(result != nil, error);
        const size_t row =
            (size_t(in.width) * 4 * sizeof(float) + 255) & ~size_t(255);
        auto buffer =
            [context->device newBufferWithLength:row * in.height
                                         options:MTLResourceStorageModeShared];
        Check(buffer != nil, "readback");
        auto blit = [commands blitCommandEncoder];
        Check(blit != nil, "blit encoder");
        [blit copyFromTexture:result
                         sourceSlice:0
                         sourceLevel:0
                        sourceOrigin:MTLOriginMake(0, 0, 0)
                          sourceSize:MTLSizeMake(in.width, in.height, 1)
                            toBuffer:buffer
                   destinationOffset:0
              destinationBytesPerRow:row
            destinationBytesPerImage:row * in.height];
        [blit endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        Check(commands.status == MTLCommandBufferStatusCompleted,
              rex::ui::metal::MetalError(commands.error, "GPU execution"));
        std::ofstream output(std::filesystem::path(argv[3]) /
                                 (path.stem().string() + ".rgba32f"),
                             std::ios::binary);
        for (uint32_t y = 0; y < in.height; ++y)
          output.write(static_cast<const char *>(buffer.contents) + y * row,
                       in.width * 4 * sizeof(float));
        Check(bool(output), "result write");
        std::vector<float> original(in.scene.size());
        [scene getBytes:original.data()
            bytesPerRow:in.width * 4 * sizeof(float)
             fromRegion:MTLRegionMake2D(0, 0, in.width, in.height)
            mipmapLevel:0];
        Check(original == in.scene, "DoF modified the title source");
        std::cout << path.stem().string() << ": completed, source preserved\n";
      }
      return 0;
    } catch (const std::exception &e) {
      std::cerr << e.what() << '\n';
      return 1;
    }
  }
}
