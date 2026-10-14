#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Pixel = std::array<float, 4>;
void Check(bool value, const std::string& message) {
  if (!value) throw std::runtime_error(message);
}
NSString* Path(const std::string& root, const std::string& file) {
  return [NSString stringWithUTF8String:(root + "/" + file).c_str()];
}
id<MTLBuffer> Buffer(id<MTLDevice> device, const std::string& root, const std::string& file) {
  NSData* bytes = [NSData dataWithContentsOfFile:Path(root, file)];
  Check(bytes != nil, "Missing fixture " + file);
  auto result = [device newBufferWithBytes:bytes.bytes length:bytes.length options:MTLResourceStorageModeShared];
  Check(result != nil, "Buffer allocation failed");
  return result;
}
}  // namespace

int main(int argc, char** argv) {
  @autoreleasepool {
    try {
      Check(argc == 2, "usage: metal-sky-orientation-test fixture-directory");
      const std::string root = argv[1];
      auto device = MTLCreateSystemDefaultDevice();
      Check(device != nil, "Metal device unavailable");
      auto queue = [device newCommandQueue];
      auto vertices = Buffer(device, root, "vertices.bin");
      auto constants = Buffer(device, root, "constants.bin");
      NSError* error = nil;
      // Exercise the actual sky vertex stage and its authored sky-color varying.
      auto fragment = [device newLibraryWithSource:@R"(
        #include <metal_stdlib>
        using namespace metal;
        struct Sky { float4 color [[user(TEXCOORD5)]]; };
        fragment float4 skyPixel(Sky in [[stage_in]]) { return float4(in.color.xyz, 1.0); }
      )" options:nil error:&error];
      Check(fragment != nil, error ? error.localizedDescription.UTF8String : "Fragment compilation failed");
      std::array<id<MTLRenderPipelineState>, 2> pipelines{};
      const std::array<std::string, 2> names{"stock", "modern"};
      for (size_t i = 0; i < pipelines.size(); ++i) {
        auto library = [device newLibraryWithURL:[NSURL fileURLWithPath:Path(root, names[i] + ".metallib")] error:&error];
        Check(library != nil, "Sky library unavailable: " + names[i]);
        auto descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = [library newFunctionWithName:@"shaderMain"];
        descriptor.fragmentFunction = [fragment newFunctionWithName:@"skyPixel"];
        descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA32Float;
        auto layout = [MTLVertexDescriptor vertexDescriptor];
        layout.attributes[0].format = MTLVertexFormatFloat4;
        layout.attributes[0].bufferIndex = 9;
        layout.attributes[1].format = MTLVertexFormatFloat4;
        layout.attributes[1].bufferIndex = 9;
        layout.attributes[1].offset = sizeof(Pixel);
        layout.layouts[9].stride = sizeof(std::array<Pixel, 2>);
        descriptor.vertexDescriptor = layout;
        pipelines[i] = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        Check(pipelines[i] != nil, error ? error.localizedDescription.UTF8String : "Sky pipeline failed");
      }
      size_t cases = 0, mismatched_cases = 0;
      for (const auto* offset : {"zero", "half"}) {
        auto shared = Buffer(device, root, std::string("shared-") + offset + ".bin");
        for (auto winding : {MTLWindingClockwise, MTLWindingCounterClockwise}) {
          for (auto cull : {MTLCullModeNone, MTLCullModeFront, MTLCullModeBack}) {
            std::array<std::vector<Pixel>, 2> results;
            std::array<size_t, 2> covered{};
            for (size_t i = 0; i < pipelines.size(); ++i) {
              auto texture_desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                  width:64 height:64 mipmapped:NO];
              texture_desc.storageMode = MTLStorageModeShared;
              texture_desc.usage = MTLTextureUsageRenderTarget;
              auto target = [device newTextureWithDescriptor:texture_desc];
              Check(target != nil, "Target allocation failed");
              auto commands = [queue commandBuffer];
              auto pass = [MTLRenderPassDescriptor renderPassDescriptor];
              pass.colorAttachments[0].texture = target;
              pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
              pass.colorAttachments[0].loadAction = MTLLoadActionClear;
              pass.colorAttachments[0].storeAction = MTLStoreActionStore;
              auto encoder = [commands renderCommandEncoderWithDescriptor:pass];
              [encoder setRenderPipelineState:pipelines[i]];
              [encoder setVertexBuffer:vertices offset:0 atIndex:9];
              const std::array<uint64_t, 3> push{constants.gpuAddress, 0, shared.gpuAddress};
              [encoder setVertexBytes:push.data() length:sizeof(push) atIndex:8];
              [encoder useResource:constants usage:MTLResourceUsageRead stages:MTLRenderStageVertex];
              [encoder useResource:shared usage:MTLResourceUsageRead stages:MTLRenderStageVertex];
              [encoder setFrontFacingWinding:winding];
              [encoder setCullMode:cull];
              [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
              [encoder endEncoding];
              [commands commit];
              [commands waitUntilCompleted];
              Check(commands.status == MTLCommandBufferStatusCompleted, "Sky GPU command failed");
              results[i].resize(4096);  // 64 * 64, derived by the Python fixture.
              [target getBytes:results[i].data() bytesPerRow:1024
                  fromRegion:MTLRegionMake2D(0, 0, 64, 64) mipmapLevel:0];
              for (const auto& p : results[i]) if (p[3] > 0.5f) ++covered[i];
            }
            std::printf("sky offset=%s winding=%lu cull=%lu stock_pixels=%zu modern_pixels=%zu\n",
                offset, (unsigned long)winding, (unsigned long)cull, covered[0], covered[1]);
            if (cull == MTLCullModeNone) Check(covered[0] > 0, "Stock sky fixture did not rasterize");
            bool matches = true;
            for (size_t pixel = 0; pixel < results[0].size(); ++pixel) {
              for (size_t component = 0; component < Pixel{}.size(); ++component) {
                const float a = results[0][pixel][component], b = results[1][pixel][component];
                matches &= std::isfinite(a) && std::isfinite(b) && std::abs(a - b) < 0.00001f;
              }
            }
            if (!matches) ++mismatched_cases;
            ++cases;
          }
        }
      }
      Check(mismatched_cases == 0, "Modern sky changed stock coverage, orientation, or color in " +
            std::to_string(mismatched_cases) + " cases");
      std::printf("PASS: %zu sky rasterization cases\n", cases);
      return 0;
    } catch (const std::exception& error) {
      std::fprintf(stderr, "FAIL: %s\n", error.what());
      return 1;
    }
  }
}
