#include "graphics/gta4_native/native_color_output.h"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
size_t checks = 0, cases = 0;
void Check(bool value, const std::string &error) {
  ++checks;
  if (!value)
    throw std::runtime_error(error);
}
struct Vertex {
  std::array<float, 4> position, color, uv;
};
struct DrawConstants {
  std::array<float, 2> extent, jitter, previous_jitter;
  uint32_t valid, reactive, ui_mode = 0, padding[3]{};
};
static_assert(sizeof(DrawConstants) == 48);
struct Push {
  uint64_t vertex, pixel, shared;
};
std::vector<uint8_t> ReadFile(const std::string &p) {
  std::ifstream f(p, std::ios::binary);
  return {(std::istreambuf_iterator<char>(f)), {}};
}
id<MTLLibrary> Library(id<MTLDevice> d, const std::string &p) {
  auto b = ReadFile(p);
  Check(b.size() > 4, "missing motion shader fixture " + p);
  auto data = dispatch_data_create(b.data(), b.size(), nullptr,
                                   DISPATCH_DATA_DESTRUCTOR_DEFAULT);
  NSError *e = nil;
  auto l = [d newLibraryWithData:data error:&e];
  Check(l != nil, e ? e.localizedDescription.UTF8String : "motion shader load");
  return l;
}
id<MTLFunction> Function(id<MTLLibrary> l, uint32_t specialization,
                         bool ui = false) {
  auto constants = [MTLFunctionConstantValues new];
  [constants setConstantValue:&specialization type:MTLDataTypeUInt atIndex:0];
  NSError *e = nil;
  auto f = [l newFunctionWithName:ui ? @"shaderMainUI" : @"shaderMain"
                   constantValues:constants
                            error:&e];
  Check(f != nil,
        e ? e.localizedDescription.UTF8String : "motion shader function");
  return f;
}
} // namespace
int main(int argc, char **argv) {
  @autoreleasepool {
    try {
      Check(argc == 2, "usage: motion-shader-test fixture-directory");
      const std::string root = argv[1];
      auto device = MTLCreateSystemDefaultDevice();
      auto queue = [device newCommandQueue];
      auto vs = Library(device, root + "/vertex.metallib"),
           ps = Library(device, root + "/pixel.metallib"),
           late = Library(device, root + "/pixel-late.metallib");
      const auto texture = [&](MTLPixelFormat format, uint32_t size,
                               MTLStorageMode mode) {
        auto d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                                    width:size
                                                                   height:size
                                                                mipmapped:NO];
        d.storageMode = mode;
        d.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
        auto t = [device newTextureWithDescriptor:d];
        Check(t != nil, "motion target");
        return t;
      };
      auto white = texture(MTLPixelFormatRGBA8Unorm, 1, MTLStorageModeShared);
      const uint32_t texel = 0xFFFFFFFF;
      [white replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
               mipmapLevel:0
                 withBytes:&texel
               bytesPerRow:4];
      auto sd = [MTLSamplerDescriptor new];
      sd.supportArgumentBuffers = YES;
      auto sampler = [device newSamplerStateWithDescriptor:sd];
      Check(sampler != nil, "argument sampler");
      auto heap = [device newBufferWithLength:26 * sizeof(uint64_t)
                                      options:MTLResourceStorageModeShared];
      auto samplers = [device newBufferWithLength:26 * sizeof(uint64_t)
                                          options:MTLResourceStorageModeShared];
      for (uint32_t i = 0; i < 26; ++i) {
        static_cast<uint64_t *>(heap.contents)[i] = white.gpuResourceID._impl;
        static_cast<uint64_t *>(samplers.contents)[i] =
            sampler.gpuResourceID._impl;
      }
      auto cb = [device newBufferWithLength:4096
                                    options:MTLResourceStorageModeShared];
      auto previous = [device newBufferWithLength:4096
                                          options:MTLResourceStorageModeShared];
      auto pb = [device newBufferWithLength:4096
                                    options:MTLResourceStorageModeShared];
      auto shared = [device newBufferWithLength:1536
                                        options:MTLResourceStorageModeShared];
      auto old_shared =
          [device newBufferWithLength:1536
                              options:MTLResourceStorageModeShared];
      std::memset(cb.contents, 0, 4096);
      std::memset(previous.contents, 0, 4096);
      std::memset(pb.contents, 0, 4096);
      std::memset(shared.contents, 0, 1536);
      std::memset(old_shared.contents, 0, 1536);
      auto word = [](id<MTLBuffer> b, size_t offset, float value) {
        std::memcpy(static_cast<uint8_t *>(b.contents) + offset, &value,
                    sizeof(value));
      };
      for (size_t row = 0; row < 4; ++row) {
        word(cb, (8 + row) * 16 + row * 4, 1);
        word(previous, (8 + row) * 16 + row * 4, 1);
      }
      for (size_t offset : {624u, 628u, 736u})
        word(pb, offset, 1);
      word(shared, 580, 0.5f);
      word(shared, 744, 1);
      word(shared, 748, 1);
      static_cast<uint32_t *>(shared.contents)[740 / 4] = 1;
      for (size_t i = 0; i < 4; ++i) {
        auto p = rex::graphics::gta4_native::NativeColorOutputParameters{};
        std::memcpy(static_cast<uint8_t *>(shared.contents) + 864 +
                        i * sizeof(p),
                    &p, sizeof(p));
      }
      std::memcpy(old_shared.contents, shared.contents, 1536);
      const std::array<Vertex, 6> vertices{{{{-0.75f, 0.75f, 0.5f, 1},
                                             {0.25f, 0.5f, 0.75f, 1},
                                             {0.5f, 0.5f, 0, 0}},
                                            {{0.75f, 0.75f, 0.5f, 1},
                                             {0.25f, 0.5f, 0.75f, 1},
                                             {0.5f, 0.5f, 0, 0}},
                                            {{-0.75f, -0.75f, 0.5f, 1},
                                             {0.25f, 0.5f, 0.75f, 1},
                                             {0.5f, 0.5f, 0, 0}},
                                            {{0.75f, 0.75f, 0.5f, 1},
                                             {0.25f, 0.5f, 0.75f, 1},
                                             {0.5f, 0.5f, 0, 0}},
                                            {{0.75f, -0.75f, 0.5f, 1},
                                             {0.25f, 0.5f, 0.75f, 1},
                                             {0.5f, 0.5f, 0, 0}},
                                            {{-0.75f, -0.75f, 0.5f, 1},
                                             {0.25f, 0.5f, 0.75f, 1},
                                             {0.5f, 0.5f, 0, 0}}}};
      auto vb = [device newBufferWithBytes:vertices.data()
                                    length:sizeof(vertices)
                                   options:MTLResourceStorageModeShared];
      auto vd = [MTLVertexDescriptor vertexDescriptor];
      vd.layouts[9].stride = sizeof(Vertex);
      for (size_t i = 0; i < 3; ++i) {
        vd.attributes[i].format = MTLVertexFormatFloat4;
        vd.attributes[i].bufferIndex = 9;
      }
      vd.attributes[0].offset = offsetof(Vertex, position);
      vd.attributes[1].offset = offsetof(Vertex, uv);
      vd.attributes[2].offset = offsetof(Vertex, color);
      // Additional modes exercise actual generated vertex/fragment variants
      // under camera/object Z motion, perspective interpolation, and depth
      // viewports for which native temporal history must be explicitly invalid.
      for (uint32_t depth_mode = 0; depth_mode < 9; ++depth_mode)
      for (uint32_t mask_mode : {0u, 7u, 8u})
        for (bool valid : {false, true})
          for (bool reactive : {false, true})
            for (bool reject_alpha : {false, true})
              for (bool jittered : {false, true}) {
                const bool destination_mask = mask_mode != 0;
                if (destination_mask && reject_alpha)
                  continue;
                if (depth_mode && (destination_mask || !valid || reactive || reject_alpha || jittered))
                  continue;
                DrawConstants c{
                    {64, 64},
                    jittered ? std::array<float, 2>{0.0078125f, -0.0078125f}
                             : std::array<float, 2>{0, 0},
                    jittered ? std::array<float, 2>{-0.01171875f, 0.00390625f}
                             : std::array<float, 2>{0, 0},
                    uint32_t(valid),
                    uint32_t(reactive)};
                c.ui_mode = mask_mode;
                word(cb, (8 + 3) * 16, 0);
                word(cb, (8 + 3) * 16 + 4, 0);
                word(previous, (8 + 3) * 16, -0.125f);
                word(previous, (8 + 3) * 16 + 4, 0.0625f);
                word(shared, 552, 0.015625f);
                word(shared, 556, -0.015625f);
                word(old_shared, 552, 0.015625f);
                word(old_shared, 556, -0.015625f);
                word(shared, 1264, depth_mode == 5 ? 0.2f : 0.0f);
                word(shared, 1268, depth_mode == 5 ? 0.8f : 1.0f);
                word(old_shared, 1264, depth_mode == 6 ? 0.1f : 0.0f);
                word(old_shared, 1268, depth_mode == 6 ? 0.9f : 1.0f);
                const double current_view_z = depth_mode == 2 ? 10.5 : depth_mode == 3 ? 8.0 : 9.5;
                const bool reversed_depth = depth_mode >= 7,
                           sloped_depth = depth_mode == 4 || depth_mode == 8;
                constexpr double previous_view_z = 10, near_plane = 0.1, far_plane = 1000;
                const double projection_a = reversed_depth ? near_plane / (near_plane - far_plane)
                                                          : far_plane / (far_plane - near_plane),
                             projection_b = (reversed_depth ? 1 : -1) * near_plane * far_plane / (far_plane - near_plane);
                if (depth_mode) {
                  for (auto buffer : {cb, previous})
                    for (size_t row = 0; row < 4; ++row)
                      for (size_t column = 0; column < 4; ++column)
                        word(buffer, (8 + row) * 16 + column * 4, 0);
                  const auto perspective = [&](id<MTLBuffer> buffer, double view_z) {
                    word(buffer, 8 * 16, 1);
                    word(buffer, 9 * 16 + 4, 1);
                    word(buffer, 10 * 16 + 8, float(projection_a));
                    word(buffer, 10 * 16 + 12, 1);
                    word(buffer, 11 * 16 + 8, float(projection_a * (view_z - 0.5) + projection_b));
                    word(buffer, 11 * 16 + 12, float(view_z - 0.5));
                  };
                  perspective(cb, current_view_z);
                  perspective(previous, previous_view_z);
                  auto geometry = vertices;
                  for (auto &vertex : geometry) {
                    vertex.position[0] *= 10;
                    vertex.position[1] *= 10;
                    if (sloped_depth) vertex.position[2] += 0.1f * vertex.position[0];
                  }
                  std::memcpy(vb.contents, geometry.data(), sizeof(geometry));
                }
                Push current{cb.gpuAddress, pb.gpuAddress, shared.gpuAddress},
                    before{previous.gpuAddress, pb.gpuAddress,
                           old_shared.gpuAddress};
                auto color = texture(MTLPixelFormatRGBA16Float, 64,
                                     MTLStorageModePrivate),
                     motion = texture(MTLPixelFormatRG16Float, 64,
                                      MTLStorageModePrivate),
                     mask = texture(MTLPixelFormatR8Unorm, 64,
                                    MTLStorageModePrivate);
                auto ui_add = texture(destination_mask ? MTLPixelFormatRGBA16Float : MTLPixelFormatR32Float, 64,
                                      MTLStorageModePrivate),
                     ui_transmit = texture(MTLPixelFormatRGBA16Float, 64,
                                           MTLStorageModePrivate);
                auto pd = [MTLRenderPipelineDescriptor new];
                pd.vertexDescriptor = vd;
                pd.vertexFunction = Function(vs, 0);
                pd.fragmentFunction =
                    Function(reject_alpha ? late : ps, reject_alpha ? 2 : 0,
                             destination_mask);
                pd.colorAttachments[0].pixelFormat = color.pixelFormat;
                pd.colorAttachments[4].pixelFormat = motion.pixelFormat;
                pd.colorAttachments[5].pixelFormat = mask.pixelFormat;
                pd.colorAttachments[6].pixelFormat = ui_add.pixelFormat;
                pd.colorAttachments[7].pixelFormat = ui_transmit.pixelFormat;
                if (destination_mask) {
                  auto original = pd.colorAttachments[0];
                  original.blendingEnabled = YES;
                  original.sourceRGBBlendFactor =
                      original.sourceAlphaBlendFactor =
                          MTLBlendFactorDestinationAlpha;
                  original.destinationRGBBlendFactor =
                      original.destinationAlphaBlendFactor =
                          mask_mode == 7
                              ? MTLBlendFactorOneMinusDestinationAlpha
                              : MTLBlendFactorOne;
                  auto add = pd.colorAttachments[6];
                  add.blendingEnabled = YES;
                  add.sourceRGBBlendFactor = MTLBlendFactorOne;
                  add.destinationRGBBlendFactor =
                      mask_mode == 7 ? MTLBlendFactorOneMinusSourceAlpha
                                     : MTLBlendFactorOne;
                  auto transmit = pd.colorAttachments[7];
                  transmit.blendingEnabled = YES;
                  transmit.sourceRGBBlendFactor = MTLBlendFactorZero;
                  transmit.destinationRGBBlendFactor =
                      MTLBlendFactorSourceColor;
                }
                NSError *e = nil;
                auto pipeline =
                    [device newRenderPipelineStateWithDescriptor:pd error:&e];
                Check(pipeline != nil, e ? e.localizedDescription.UTF8String
                                         : "motion pipeline");
                auto cmd = [queue commandBuffer];
                auto pass = [MTLRenderPassDescriptor renderPassDescriptor];
                for (size_t i : {0u, 4u, 5u, 6u, 7u}) {
                  pass.colorAttachments[i].loadAction = MTLLoadActionClear;
                  pass.colorAttachments[i].storeAction = MTLStoreActionStore;
                  pass.colorAttachments[i].clearColor =
                      MTLClearColorMake(0, 0, 0, 0);
                }
                pass.colorAttachments[0].texture = color;
                pass.colorAttachments[4].texture = motion;
                pass.colorAttachments[5].texture = mask;
                pass.colorAttachments[6].texture = ui_add;
                pass.colorAttachments[7].texture = ui_transmit;
                pass.colorAttachments[7].clearColor =
                    MTLClearColorMake(1, 1, 1, 1);
                if (!destination_mask)
                  pass.colorAttachments[6].clearColor = MTLClearColorMake(-1, 0, 0, 0);
                if (destination_mask)
                  pass.colorAttachments[0].clearColor =
                      MTLClearColorMake(0.4, 0.2, 0.6, 0.25);
                auto encoder = [cmd renderCommandEncoderWithDescriptor:pass];
                Check(encoder != nil, "motion encoder");
                [encoder setRenderPipelineState:pipeline];
                [encoder setViewport:MTLViewport{0, 0, 64, 64,
                    depth_mode == 5 ? 0.2 : 0.0, depth_mode == 5 ? 0.8 : 1.0}];
                [encoder setVertexBuffer:vb offset:0 atIndex:9];
                for (size_t i = 0; i < 4; ++i) {
                  [encoder setVertexBuffer:heap offset:0 atIndex:i];
                  [encoder setFragmentBuffer:heap offset:0 atIndex:i];
                }
                [encoder setVertexBuffer:samplers offset:0 atIndex:4];
                [encoder setFragmentBuffer:samplers offset:0 atIndex:4];
                [encoder setVertexBytes:&c length:sizeof(c) atIndex:6];
                [encoder setFragmentBytes:&c length:sizeof(c) atIndex:6];
                [encoder setVertexBytes:&before
                                 length:sizeof(before)
                                atIndex:7];
                [encoder setVertexBytes:&current
                                 length:sizeof(current)
                                atIndex:8];
                [encoder setFragmentBytes:&current
                                   length:sizeof(current)
                                  atIndex:8];
                for (id<MTLResource> r : {cb, previous, pb, shared, old_shared})
                  [encoder useResource:r
                                 usage:MTLResourceUsageRead
                                stages:MTLRenderStageVertex |
                                       MTLRenderStageFragment];
                [encoder
                    useResource:white
                          usage:MTLResourceUsageRead
                         stages:MTLRenderStageVertex | MTLRenderStageFragment];
                [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                            vertexStart:0
                            vertexCount:6];
                if (destination_mask)
                  [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                              vertexStart:0
                              vertexCount:6];
                [encoder endEncoding];
                std::array<id<MTLBuffer>, 5> outputs{};
                std::array<id<MTLTexture>, 5> textures{color, motion, mask,
                                                       ui_add, ui_transmit};
                auto blit = [cmd blitCommandEncoder];
                for (size_t i = 0; i < 5; ++i) {
                  outputs[i] =
                      [device newBufferWithLength:512 * 64
                                          options:MTLResourceStorageModeShared];
                  [blit copyFromTexture:textures[i]
                                   sourceSlice:0
                                   sourceLevel:0
                                  sourceOrigin:MTLOriginMake(0, 0, 0)
                                    sourceSize:MTLSizeMake(64, 64, 1)
                                      toBuffer:outputs[i]
                             destinationOffset:0
                        destinationBytesPerRow:512
                      destinationBytesPerImage:512 * 64];
                }
                [blit endEncoding];
                [cmd commit];
                [cmd waitUntilCompleted];
                Check(cmd.status == MTLCommandBufferStatusCompleted,
                      cmd.error ? cmd.error.localizedDescription.UTF8String
                                : "motion GPU failure");
                std::array<float, 4> expected_color =
                    reject_alpha ? std::array<float, 4>{}
                                 : std::array<float, 4>{0.25f, 0.5f, 0.75f, 1};
                if (destination_mask) {
                  expected_color = {0.4f, 0.2f, 0.6f, 0.25f};
                  for (int iteration = 0; iteration < 2; ++iteration) {
                    const float alpha = expected_color[3];
                    for (size_t channel = 0; channel < 4; ++channel)
                      expected_color[channel] =
                          std::array<float, 4>{0.25f, 0.5f, 0.75f, 1}[channel] *
                              alpha +
                          expected_color[channel] *
                              (mask_mode == 7 ? 1 - alpha : 1);
                  }
                }
                for (size_t y = 16; y < 48; ++y)
                  for (size_t x = 16; x < 48; ++x) {
                    const auto half = [&](size_t b, size_t stride,
                                          size_t channel) {
                      _Float16 v;
                      std::memcpy(
                          &v,
                          static_cast<const uint8_t *>(outputs[b].contents) +
                              y * 512 + x * stride + channel * 2,
                          2);
                      return float(v);
                    };
                    for (size_t k = 0; k < 4; ++k) {
                      const float expected = expected_color[k];
                      if (std::abs(half(0, 8, k) - expected) >= 0.001f) {
                        std::fprintf(
                            stderr,
                            "motion color valid=%u reactive=%u discard=%u "
                            "jitter=%u xy=%zu,%zu channel=%zu wanted=%g "
                            "actual=%g motion=%g,%g\n",
                            valid, reactive, reject_alpha, jittered, x, y, k,
                            expected, half(0, 8, k), half(1, 4, 0),
                            half(1, 4, 1));
                        Check(false, "motion wrapper changed original shader "
                                     "color/discard");
                      }
                    }
                    if (destination_mask)
                      for (size_t channel = 0; channel < 3; ++channel) {
                        const float original = std::array<float, 4>{
                            0.4f, 0.2f, 0.6f, 0.25f}[channel];
                        Check(std::abs(half(3, 8, channel) +
                                       original * half(4, 8, channel) -
                                       half(0, 8, channel)) < 0.004f,
                              "destination-alpha HUD isolation disagrees with "
                              "the original framebuffer");
                      }
                    double expected_x = valid && !reject_alpha ? -4 : 0,
                           expected_y = valid && !reject_alpha ? -2 : 0,
                           expected_depth = valid && !reject_alpha ? 0.5 : -1;
                    if (depth_mode) {
                      // Recover the point on the current plane independently
                      // from pixel position, then project it in the old camera.
                      const double nx = 2 * (double(x) + 0.5) / 64 - 1 - 0.015625,
                                   ny = 1 - 2 * (double(y) + 0.5) / 64 + 0.015625;
                      const double current_z = current_view_z / (1 - (sloped_depth ? 0.1 : 0.0) * nx),
                                   previous_z = current_z + previous_view_z - current_view_z;
                      expected_x = (nx * current_z / previous_z - nx) * 32;
                      expected_y = (ny * current_z / previous_z - ny) * -32;
                      expected_depth = depth_mode == 5 || depth_mode == 6 ? -1 : projection_a + projection_b / previous_z;
                    }
                    Check(std::abs(half(1, 4, 0) - expected_x) < 0.01 &&
                              std::abs(half(1, 4, 1) - expected_y) < 0.01,
                          "generated motion mode=" + std::to_string(depth_mode) +
                              " xy=" + std::to_string(x) + "," + std::to_string(y) +
                              " actual=" + std::to_string(half(1, 4, 0)) + "," +
                              std::to_string(half(1, 4, 1)) + " expected=" +
                              std::to_string(expected_x) + "," + std::to_string(expected_y));
                    if (!destination_mask) {
                      float actual_depth;
                      std::memcpy(&actual_depth,
                          static_cast<const uint8_t *>(outputs[3].contents) + y * 512 + x * sizeof(float),
                          sizeof(actual_depth));
                      Check(std::abs(actual_depth - expected_depth) < 0.000003,
                            "generated previous depth mode=" + std::to_string(depth_mode) +
                                " xy=" + std::to_string(x) + "," + std::to_string(y) +
                                " actual=" + std::to_string(actual_depth) +
                                " expected=" + std::to_string(expected_depth));
                    }
                    const auto r = static_cast<const uint8_t *>(
                        outputs[2].contents)[y * 512 + x];
                    Check(
                        r == (reject_alpha         ? 0
                              : !valid || reactive ? 255
                                                   : 0),
                        "invalid-history/reactive output or discard incorrect");
                  }
                ++cases;
              }
      std::printf(
          "stock_temporal_motion_gpu=passed cases=%zu checks=%zu device=%s\n",
          cases, checks, device.name.UTF8String);
      return 0;
    } catch (const std::exception &e) {
      std::fprintf(stderr, "Motion shader test: %s\n", e.what());
      return 1;
    }
  }
}
