#include "output_transfer.h"
#include "context.h"
#include <cmath>
#include <algorithm>
#include <limits>

namespace rex::ui::metal {
bool OutputTransfer::Initialize(std::shared_ptr<MetalContext> context, std::string& error) {
  @autoreleasepool {
    error.clear();
    if (pipelines_[0]) return true;
    if (!context || !context->device || !context->ui_library || !context->pass_library) {
      error = "Output transfer requires the existing Metal libraries"; return false;
    }
    auto vertex = [context->ui_library newFunctionWithName:@"liberty_present_vertex"];
    auto pixel = [context->pass_library newFunctionWithName:@"liberty_hdr_present_ps"];
    if (!vertex || !pixel) { error = "Missing authored output-transfer shader"; return false; }
    constexpr MTLPixelFormat formats[] = {MTLPixelFormatBGRA8Unorm, MTLPixelFormatRGBA16Float};
    std::array<id<MTLRenderPipelineState>, 2> candidates{};
    for (size_t i = 0; i < candidates.size(); ++i) {
      auto descriptor = [MTLRenderPipelineDescriptor new];
      descriptor.vertexFunction = vertex;
      descriptor.fragmentFunction = pixel;
      descriptor.colorAttachments[0].pixelFormat = formats[i];
      descriptor.label = @"Liberty authored output transfer";
      NSError* native_error = nil;
      candidates[i] = [context->device newRenderPipelineStateWithDescriptor:descriptor error:&native_error];
      if (!candidates[i]) { error = MetalError(native_error, "Output-transfer pipeline creation failed"); return false; }
    }
    auto descriptor = [MTLSamplerDescriptor new];
    descriptor.minFilter = descriptor.magFilter = MTLSamplerMinMagFilterLinear;
    descriptor.sAddressMode = descriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
    auto sampler = [context->device newSamplerStateWithDescriptor:descriptor];
    if (!sampler) { error = "Output-transfer sampler allocation failed"; return false; }
    auto fused=[context->pass_library newFunctionWithName:@"liberty_hdr_bilinear_present_ps"];
    if(!fused){error="Missing fused bilinear HDR shader";return false;}
    std::array<id<MTLRenderPipelineState>,2> fused_candidates{};
    for(size_t i=0;i<fused_candidates.size();++i){
      auto d=[MTLRenderPipelineDescriptor new];d.vertexFunction=vertex;d.fragmentFunction=fused;
      d.colorAttachments[0].pixelFormat=formats[i];d.label=@"Liberty fused bilinear and HDR";
      NSError* e=nil;fused_candidates[i]=[context->device newRenderPipelineStateWithDescriptor:d error:&e];
      if(!fused_candidates[i]){error=MetalError(e,"Fused HDR pipeline creation failed");return false;}
    }
    bilinear_hdr_=fused_candidates;
    pipelines_ = candidates;
    sampler_ = sampler;
    return true;
  }
}

bool OutputTransfer::Draw(id<MTLRenderCommandEncoder> encoder, id<MTLTexture> source,
    id<MTLTexture> destination, OutputTransferConstants constants, std::string& error) const {
  error.clear();
  if (!encoder || !source || !destination || source == destination || !sampler_ ||
      source.textureType != MTLTextureType2D || destination.textureType != MTLTextureType2D ||
      !source.width || !source.height || !destination.width || !destination.height ||
      (destination.pixelFormat != MTLPixelFormatBGRA8Unorm && destination.pixelFormat != MTLPixelFormatRGBA16Float) ||
      constants.hdr_mode > 2 || !std::isfinite(constants.hdr_headroom) || constants.hdr_headroom < 1 ||
      !std::isfinite(constants.paper_white_nits) || constants.paper_white_nits <= 0 ||
      !std::isfinite(constants.peak_nits) || constants.peak_nits <= 0 ||
      !std::isfinite(constants.shoulder_start) || !std::isfinite(constants.shoulder_power)) {
    error = "Invalid Metal output-transfer request"; return false;
  }
  constants.source_extent[0] = int32_t(source.width);
  constants.source_extent[1] = int32_t(source.height);
  constants.destination_extent[0] = int32_t(destination.width);
  constants.destination_extent[1] = int32_t(destination.height);
  const size_t index = destination.pixelFormat == MTLPixelFormatBGRA8Unorm ? 0 : 1;
  [encoder setRenderPipelineState:pipelines_[index]];
  [encoder setCullMode:MTLCullModeNone];
  [encoder setViewport:MTLViewport{0, 0, double(destination.width), double(destination.height), 0, 1}];
  [encoder setScissorRect:MTLScissorRect{0, 0, destination.width, destination.height}];
  [encoder setFragmentTexture:source atIndex:0];
  [encoder setFragmentSamplerState:sampler_ atIndex:0];
  [encoder setFragmentBytes:&constants length:sizeof(constants) atIndex:0];
  [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
  return true;
}

bool OutputTransfer::DrawBilinearHdr(id<MTLRenderCommandEncoder> encoder,id<MTLTexture> source,
    id<MTLTexture> target,MTLViewport viewport,OutputTransferConstants constants,std::string& error) const {
  error.clear();
  const auto integral=[](double value){return std::isfinite(value)&&value>=std::numeric_limits<int32_t>::min()&&
      value<=std::numeric_limits<int32_t>::max()&&std::trunc(value)==value;};
  if(!encoder||!source||!target||source==target||!sampler_||!bilinear_hdr_[0]||
      source.textureType!=MTLTextureType2D||target.textureType!=MTLTextureType2D||source.sampleCount!=1||target.sampleCount!=1||
      !source.width||!source.height||!target.width||!target.height||
      (target.pixelFormat!=MTLPixelFormatBGRA8Unorm&&target.pixelFormat!=MTLPixelFormatRGBA16Float)||
      !integral(viewport.originX)||!integral(viewport.originY)||!integral(viewport.width)||!integral(viewport.height)||
      viewport.width<=0||viewport.height<=0||constants.hdr_mode<1||constants.hdr_mode>2||constants.output_mode!=4||
      !std::isfinite(constants.hdr_headroom)||constants.hdr_headroom<1||
      !std::isfinite(constants.paper_white_nits)||constants.paper_white_nits<=0||
      !std::isfinite(constants.peak_nits)||constants.peak_nits<=0||
      !std::isfinite(constants.shoulder_start)||!std::isfinite(constants.shoulder_power)){
    error="Invalid fused bilinear/HDR request";return false;
  }
  struct alignas(16) Parameters {OutputTransferConstants hdr;std::array<int32_t,2> origin,extent;};
  static_assert(sizeof(Parameters)==64);
  constants.source_extent[0]=int32_t(source.width);constants.source_extent[1]=int32_t(source.height);
  constants.destination_extent[0]=int32_t(target.width);constants.destination_extent[1]=int32_t(target.height);
  const Parameters p{constants,{int32_t(viewport.originX),int32_t(viewport.originY)},
      {int32_t(viewport.width),int32_t(viewport.height)}};
  const double left=std::clamp(viewport.originX,0.0,double(target.width)),top=std::clamp(viewport.originY,0.0,double(target.height));
  const double right=std::clamp(viewport.originX+viewport.width,left,double(target.width));
  const double bottom=std::clamp(viewport.originY+viewport.height,top,double(target.height));
  if(right<=left||bottom<=top)return true;
  [encoder setRenderPipelineState:bilinear_hdr_[target.pixelFormat==MTLPixelFormatBGRA8Unorm?0:1]];
  [encoder setCullMode:MTLCullModeNone];[encoder setViewport:viewport];
  [encoder setScissorRect:MTLScissorRect{NSUInteger(left),NSUInteger(top),NSUInteger(right-left),NSUInteger(bottom-top)}];
  [encoder setFragmentTexture:source atIndex:0];[encoder setFragmentSamplerState:sampler_ atIndex:0];
  [encoder setFragmentBytes:&p length:sizeof(p) atIndex:0];[encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
  return true;
}
}  // namespace rex::ui::metal
