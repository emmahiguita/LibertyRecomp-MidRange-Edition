#include "modern_sky_probe.h"
#include <rex/logging.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace rex::graphics::gta4_metal {
namespace {
// Read the same fixed grid before/after the draw, including multisample sky
// reflections. This is a spatial sample, not a claim about every output pixel.
NSString* const kSource = @R"metal(
#include <metal_stdlib>
using namespace metal;
uint2 probe_position(uint index, uint width, uint height) {
  return min(uint2(width-1,height-1),
      uint2(((index%32)*2+1)*width/64, ((index/32)*2+1)*height/36));
}
kernel void sky_single(texture2d<float,access::read> image [[texture(0)]],
    device float4* result [[buffer(0)]], uint i [[thread_position_in_grid]]) {
  if(i<576) result[i]=image.read(probe_position(i,image.get_width(),image.get_height()));
}
kernel void sky_msaa(texture2d_ms<float,access::read> image [[texture(0)]],
    device float4* result [[buffer(0)]], uint i [[thread_position_in_grid]]) {
  if(i>=576)return;
  uint2 p=probe_position(i,image.get_width(),image.get_height());
  float4 value=0;
  for(uint s=0;s<image.get_num_samples();++s)value+=image.read(p,s);
  result[i]=value/float(image.get_num_samples());
}
)metal";
std::string Error(NSError* error, const char* fallback) {
  return error ? std::string(error.localizedDescription.UTF8String) : fallback;
}
}
std::shared_ptr<ModernSkyProbe::Capture> ModernSkyProbe::Start(id<MTLDevice> device,
    id<MTLCommandBuffer> commands, id<MTLTexture> image, std::string context, std::string& error) {
  if (!commands || !image || (image.textureType != MTLTextureType2D &&
      image.textureType != MTLTextureType2DMultisample)) { error="unsupported sky probe texture"; return {}; }
  if (!single_ || !multisample_) {
    NSError* failure = nil;
    auto options = [MTLCompileOptions new]; options.fastMathEnabled = NO;
    auto library = [device newLibraryWithSource:kSource options:options error:&failure];
    if (!library) { error=Error(failure,"sky probe library creation failed"); return {}; }
    single_ = [device newComputePipelineStateWithFunction:[library newFunctionWithName:@"sky_single"] error:&failure];
    multisample_ = [device newComputePipelineStateWithFunction:[library newFunctionWithName:@"sky_msaa"] error:&failure];
    if (!single_ || !multisample_) { error=Error(failure,"sky probe pipeline creation failed"); return {}; }
  }
  auto capture = std::make_shared<Capture>(); capture->context=std::move(context);
  capture->before=[device newBufferWithLength:kSamples*sizeof(std::array<float,4>) options:MTLResourceStorageModeShared];
  if (!capture->before) { error="sky probe allocation failed"; return {}; }
  capture->after=[device newBufferWithLength:capture->before.length options:MTLResourceStorageModeShared];
  capture->visibility=[device newBufferWithLength:sizeof(uint64_t) options:MTLResourceStorageModeShared];
  if (!capture->before || !capture->after || !capture->visibility) { error="sky probe allocation failed"; return {}; }
  std::memset(capture->visibility.contents,0,capture->visibility.length);
  if (!Sample(commands,image,capture->before,error)) return {};
  return capture;
}
bool ModernSkyProbe::Sample(id<MTLCommandBuffer> commands,id<MTLTexture> image,
    id<MTLBuffer> output,std::string& error) {
  auto encoder=[commands computeCommandEncoder];
  if (!encoder) {error="sky probe compute encoder failed";return false;}
  encoder.label=@"Modern sky diagnostic: sample target";
  [encoder setComputePipelineState:image.sampleCount>1?multisample_:single_];
  [encoder setTexture:image atIndex:0]; [encoder setBuffer:output offset:0 atIndex:0];
  [encoder dispatchThreads:MTLSizeMake(kSamples,1,1) threadsPerThreadgroup:MTLSizeMake(32,1,1)];
  [encoder endEncoding]; return true;
}
ModernSkyProbe::Statistics ModernSkyProbe::Read(const Capture& c) {
  Statistics result;
  std::memcpy(&result.visible_samples,c.visibility.contents,sizeof(result.visible_samples));
  const auto* before=static_cast<const std::array<float,4>*>(c.before.contents);
  const auto* after=static_cast<const std::array<float,4>*>(c.after.contents);
  result.minimum=std::numeric_limits<float>::infinity();
  result.maximum=-std::numeric_limits<float>::infinity();
  for(uint32_t i=0;i<kSamples;++i) {
    result.changed+=std::memcmp(&before[i],&after[i],sizeof(before[i]))!=0;
    const auto finite=[](const auto& p){return std::all_of(p.begin(),p.end(),[](float v){return std::isfinite(v);});};
    result.invalid_before+=!finite(before[i]);
    if(!finite(after[i])){++result.invalid_after;continue;}
    bool black=true,negative=false;
    for(size_t channel=0;channel<3;++channel){
      const float v=after[i][channel];black&=std::abs(v)<=0.000001f;negative|=v<0;
      result.minimum=std::min(result.minimum,v);result.maximum=std::max(result.maximum,v);
    }
    result.black+=black;result.negative+=negative;
  }
  result.top_center=after[kColumns/2];
  return result;
}
bool ModernSkyProbe::Finish(id<MTLCommandBuffer> commands,id<MTLTexture> image,
    std::shared_ptr<Capture> capture,std::string& error) {
  if(!Sample(commands,image,capture->after,error))return false;
  // A block captures a C++ reference parameter as a reference. Accept the
  // shared_ptr by value so the asynchronous block owns a separate strong copy
  // after the draw's local shared_ptr and the renderer have been destroyed.
  [commands addCompletedHandler:^(id<MTLCommandBuffer> completed) {
    if(completed.status!=MTLCommandBufferStatusCompleted){
      REXLOG_ERROR("gta4-modern-exec: backend=metal event=sky-gpu-error {} status={} reason={}",
          capture->context,uint32_t(completed.status),Error(completed.error,"GPU submission failed"));return;
    }
    const auto s=Read(*capture);
    REXLOG_INFO("gta4-modern-exec: backend=metal event=sky-gpu {} query={} visible-samples={} "
        "grid=32x18 changed={} black={} invalid-before={} invalid-after={} negative={} rgb-min={} rgb-max={} "
        "top-center={},{},{},{} evidence=target-samples-after-depth-stencil-blend",
        capture->context,capture->queried,s.visible_samples,s.changed,s.black,s.invalid_before,s.invalid_after,
        s.negative,s.minimum,s.maximum,s.top_center[0],s.top_center[1],s.top_center[2],s.top_center[3]);
  }];
  return true;
}
}  // namespace rex::graphics::gta4_metal
