#include "immediate_drawer.h"

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>
#include <rex/logging.h>

#include "context.h"
#include "ui_draw_context.h"

namespace rex::ui::metal {
namespace {
class MetalImmediateTexture final : public ImmediateTexture {
 public:
  MetalImmediateTexture(uint32_t width, uint32_t height, id<MTLTexture> texture,
                         id<MTLSamplerState> sampler)
      : ImmediateTexture(width, height), texture(texture), sampler(sampler) {}
  id<MTLTexture> texture;
  id<MTLSamplerState> sampler;
};
}  // namespace

struct MetalImmediateDrawer::State {
  explicit State(std::shared_ptr<MetalContext> c) : context(std::move(c)) {}
  std::shared_ptr<MetalContext> context;
  std::array<id<MTLRenderPipelineState>, 2> pipelines{};
  id<MTLDepthStencilState> depth = nil;
  std::array<id<MTLSamplerState>, 4> samplers{};
  std::unique_ptr<ImmediateTexture> white;
  MetalUIDrawContext* draw_context = nullptr;
  UploadSlice vertices, indices;
  int vertex_count = 0, index_count = 0;
  id<MTLTexture> bound_texture = nil;
  id<MTLSamplerState> bound_sampler = nil;
};

MetalImmediateDrawer::MetalImmediateDrawer(std::shared_ptr<MetalContext> context)
    : state_(std::make_unique<State>(std::move(context))) {}
MetalImmediateDrawer::~MetalImmediateDrawer() = default;
std::unique_ptr<MetalImmediateDrawer> MetalImmediateDrawer::Create(
    std::shared_ptr<MetalContext> context, std::string& error) {
  auto result = std::unique_ptr<MetalImmediateDrawer>(new MetalImmediateDrawer(std::move(context)));
  return result->Initialize(error) ? std::move(result) : nullptr;
}

bool MetalImmediateDrawer::Initialize(std::string& error) {
  @autoreleasepool {
    if (!state_->context || !state_->context->device || !state_->context->ui_library) {
      error = "Missing Metal device or UI library.";
      return false;
    }
    auto descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.label = @"Liberty UI";
    descriptor.vertexFunction = [state_->context->ui_library newFunctionWithName:@"liberty_ui_vertex"];
    descriptor.fragmentFunction = [state_->context->ui_library newFunctionWithName:@"liberty_ui_fragment"];
    auto color = descriptor.colorAttachments[0];
    color.pixelFormat = kPresentationFormat;
    color.blendingEnabled = YES;
    color.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    color.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    color.rgbBlendOperation = MTLBlendOperationAdd;
    // Matches the existing ImmediateDrawer interface's non-premultiplied color policy.
    color.sourceAlphaBlendFactor = MTLBlendFactorOne;
    color.destinationAlphaBlendFactor = MTLBlendFactorOne;
    color.alphaBlendOperation = MTLBlendOperationAdd;
    NSError* native_error = nil;
    state_->pipelines[0] = [state_->context->device newRenderPipelineStateWithDescriptor:descriptor error:&native_error];
    if (!state_->pipelines[0]) { error = MetalError(native_error, "UI pipeline creation failed."); return false; }
    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
    descriptor.fragmentFunction = [state_->context->ui_library newFunctionWithName:@"liberty_ui_fragment_linear"];
    state_->pipelines[1] = [state_->context->device newRenderPipelineStateWithDescriptor:descriptor error:&native_error];
    if (!state_->pipelines[1]) { error = MetalError(native_error, "Linear UI pipeline creation failed."); return false; }
    auto depth = [[MTLDepthStencilDescriptor alloc] init];
    depth.depthCompareFunction = MTLCompareFunctionAlways;
    depth.depthWriteEnabled = NO;
    state_->depth = [state_->context->device newDepthStencilStateWithDescriptor:depth];
    if (!state_->depth) { error = "UI depth state creation failed."; return false; }
    for (size_t i = 0; i < state_->samplers.size(); ++i) {
      auto sampler = [[MTLSamplerDescriptor alloc] init];
      sampler.minFilter = sampler.magFilter = i & 1 ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
      sampler.sAddressMode = sampler.tAddressMode = i & 2 ? MTLSamplerAddressModeRepeat : MTLSamplerAddressModeClampToEdge;
      state_->samplers[i] = [state_->context->device newSamplerStateWithDescriptor:sampler];
      if (!state_->samplers[i]) { error = "UI sampler creation failed."; return false; }
    }
    const std::array<uint8_t, 4> white{255,255,255,255};
    state_->white = CreateTexture(1, 1, ImmediateTextureFilter::kNearest, false, white.data());
    if (!state_->white) { error = "UI fallback texture creation failed."; return false; }
    error.clear();
    return true;
  }
}

std::unique_ptr<ImmediateTexture> MetalImmediateDrawer::CreateTexture(
    uint32_t width, uint32_t height, ImmediateTextureFilter filter, bool repeated, const uint8_t* data) {
  @autoreleasepool {
    constexpr uint64_t kMaximumTextureBytes = 64 * 1024 * 1024;
    if (!width || !height || !data || uint64_t(width) * height * 4 > kMaximumTextureBytes) return {};
    auto descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
        width:width height:height mipmapped:NO];
    descriptor.storageMode = MTLStorageModeShared;
    descriptor.hazardTrackingMode = MTLHazardTrackingModeTracked;
    descriptor.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> texture = [state_->context->device newTextureWithDescriptor:descriptor];
    if (!texture) return {};
    texture.label = @"Liberty UI texture";
    [texture replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0 withBytes:data
        bytesPerRow:size_t(width) * 4];
    const size_t sampler_index = (filter == ImmediateTextureFilter::kLinear ? 1 : 0) | (repeated ? 2 : 0);
    return std::make_unique<MetalImmediateTexture>(width, height, texture, state_->samplers[sampler_index]);
  }
}

void MetalImmediateDrawer::Begin(UIDrawContext& context, float width, float height) {
  ImmediateDrawer::Begin(context, std::isfinite(width) ? width : 0.0f,
                                  std::isfinite(height) ? height : 0.0f);
  state_->draw_context = dynamic_cast<MetalUIDrawContext*>(&context);
  state_->bound_texture = nil;
  state_->bound_sampler = nil;
  if (!state_->draw_context || !state_->draw_context->encoder) return;
  auto encoder = state_->draw_context->encoder;
  const size_t pipeline_index = state_->draw_context->target_format == MTLPixelFormatRGBA16Float ? 1 : 0;
  if (state_->draw_context->target_format != MTLPixelFormatBGRA8Unorm && !pipeline_index) {
    state_->draw_context = nullptr; return;
  }
  [encoder setRenderPipelineState:state_->pipelines[pipeline_index]];
  [encoder setDepthStencilState:state_->depth];
  [encoder setCullMode:MTLCullModeNone];
  [encoder setTriangleFillMode:MTLTriangleFillModeFill];
  [encoder setDepthBias:0 slopeScale:0 clamp:0];
  [encoder setViewport:MTLViewport{0,0,double(context.render_target_width()),double(context.render_target_height()),0,1}];
  const std::array<float, 2> inverse{1.0f / coordinate_space_width(), 1.0f / coordinate_space_height()};
  [encoder setVertexBytes:inverse.data() length:sizeof(inverse) atIndex:1];
}

void MetalImmediateDrawer::BeginDrawBatch(const ImmediateDrawBatch& batch) {
  EndDrawBatch();
  if (!state_->draw_context || batch.vertex_count <= 0 || !batch.vertices || batch.index_count < 0 ||
      (batch.index_count && !batch.indices)) return;
  auto& uploads = state_->draw_context->uploads;
  state_->vertices = uploads.Allocate(state_->context->device, size_t(batch.vertex_count) * sizeof(ImmediateVertex), 16);
  if (!state_->vertices) { REXLOG_ERROR("gta4-metal: UI vertex upload budget exceeded"); return; }
  std::memcpy(state_->vertices.data, batch.vertices, size_t(batch.vertex_count) * sizeof(ImmediateVertex));
  if (batch.index_count) {
    state_->indices = uploads.Allocate(state_->context->device, size_t(batch.index_count) * sizeof(uint16_t), 16);
    if (!state_->indices) { state_->vertices = {}; REXLOG_ERROR("gta4-metal: UI index upload budget exceeded"); return; }
    std::memcpy(state_->indices.data, batch.indices, size_t(batch.index_count) * sizeof(uint16_t));
  }
  state_->vertex_count = batch.vertex_count;
  state_->index_count = batch.index_count;
  [state_->draw_context->encoder setVertexBuffer:state_->vertices.buffer offset:state_->vertices.offset atIndex:0];
}

void MetalImmediateDrawer::Draw(const ImmediateDraw& draw) {
  if (!state_->draw_context || !state_->vertices || draw.count <= 0 || draw.index_offset < 0 ||
      draw.base_vertex < 0 || draw.base_vertex >= state_->vertex_count) return;
  const bool indexed = bool(state_->indices);
  if (indexed) {
    if (draw.index_offset > state_->index_count || draw.count > state_->index_count - draw.index_offset) return;
  } else if (draw.count > state_->vertex_count - draw.base_vertex) return;
  auto* texture = dynamic_cast<MetalImmediateTexture*>(draw.texture ? draw.texture : state_->white.get());
  if (!texture || texture->texture.device != state_->context->device) return;
  uint32_t left, top, width, height;
  if (!ScissorToRenderTarget(draw,left,top,width,height)) return;
  auto encoder = state_->draw_context->encoder;
  [encoder setScissorRect:MTLScissorRect{left,top,width,height}];
  if (state_->bound_texture != texture->texture) {
    [encoder setFragmentTexture:texture->texture atIndex:0]; state_->bound_texture = texture->texture;
  }
  if (state_->bound_sampler != texture->sampler) {
    [encoder setFragmentSamplerState:texture->sampler atIndex:0]; state_->bound_sampler = texture->sampler;
  }
  const auto primitive = draw.primitive_type == ImmediatePrimitiveType::kLines ? MTLPrimitiveTypeLine : MTLPrimitiveTypeTriangle;
  if (indexed) {
    [encoder drawIndexedPrimitives:primitive indexCount:draw.count indexType:MTLIndexTypeUInt16
        indexBuffer:state_->indices.buffer indexBufferOffset:state_->indices.offset + size_t(draw.index_offset) * sizeof(uint16_t)
        instanceCount:1 baseVertex:draw.base_vertex baseInstance:0];
  } else {
    [encoder drawPrimitives:primitive vertexStart:draw.base_vertex vertexCount:draw.count];
  }
}
void MetalImmediateDrawer::EndDrawBatch() {
  state_->vertices = {}; state_->indices = {};
  state_->vertex_count = state_->index_count = 0;
}
void MetalImmediateDrawer::End() {
  EndDrawBatch(); state_->draw_context = nullptr;
  state_->bound_texture = nil; state_->bound_sampler = nil;
  ImmediateDrawer::End();
}
}  // namespace rex::ui::metal
