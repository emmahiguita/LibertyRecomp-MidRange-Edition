#include <metal_stdlib>
using namespace metal;
struct DepthMotionDraw {
  float2 extent,jitter,previous_jitter;
  uint valid,reactive,ui_mode,padding0,padding1,padding2;
};
struct DepthMotionInput {
  float4 current [[user(LIBERTY_CURRENT_CLIP)]];
  float4 previous [[user(LIBERTY_PREVIOUS_CLIP)]];
};
struct DepthMotionOutput {
  float2 motion [[color(4)]];
  float reactive [[color(5)]];
  float previous_depth [[color(6)]];
};
// Depth-only prepasses must produce the same object motion as their color
// passes. Runs only when the original draw has no pixel shader or alpha test.
fragment DepthMotionOutput liberty_temporal_depth_motion(DepthMotionInput input [[stage_in]],
    constant DepthMotionDraw& c [[buffer(6)]]) {
  bool valid=c.valid&&all(isfinite(input.current))&&all(isfinite(input.previous))&&
      input.current.w>1.0e-6f&&input.previous.w>1.0e-6f;
  float2 velocity=valid?(input.previous.xy/input.previous.w-input.current.xy/input.current.w)*
      c.extent*float2(0.5f,-0.5f):float2(0);
  valid=valid&&all(isfinite(velocity))&&all(abs(velocity)<=65504.0f);
  const float z=valid?input.previous.z/input.previous.w:-1.0f;
  return {valid?velocity:float2(0),(!valid||c.reactive)?1.0f:0.0f,
          isfinite(z)&&z>=0&&z<=1?z:-1.0f};
}
struct BackgroundMotionConstants {
  float4x4 reprojection;
  float2 jitter,half_pixel;
  uint2 extent;
  uint reversed,padding;
};
kernel void liberty_temporal_background_motion(
    texture2d<float,access::read> depth [[texture(0)]],
    texture2d<float,access::read_write> motion [[texture(1)]],
    texture2d<float,access::read_write> previous_depth [[texture(2)]],
    constant BackgroundMotionConstants& c [[buffer(0)]],
    uint2 pixel [[thread_position_in_grid]]) {
  if(any(pixel>=c.extent))return;
  const float far_depth=c.reversed?0.0f:1.0f;
  // Only the clear-depth environment is reconstructible without object
  // identity. Never repair missing foreground geometry using camera motion.
  if(depth.read(pixel).r!=far_depth||previous_depth.read(pixel).r>=0)return;
  const float2 uv=(float2(pixel)+0.5f-c.jitter)/float2(c.extent);
  const float2 current=(uv*2-1)*float2(1,-1)-c.half_pixel;
  const float4 previous=c.reprojection*float4(current,far_depth,1);
  if(!all(isfinite(previous))||previous.w<=1.0e-6f)return;
  const float2 velocity=(previous.xy/previous.w-current)*float2(c.extent)*float2(0.5f,-0.5f);
  if(!all(isfinite(velocity))||any(abs(velocity)>65504.0f))return;
  motion.write(float4(velocity,0,0),pixel);
  previous_depth.write(float4(far_depth),pixel);
}
struct TemporalConstants {
  uint2 extent;float2 jitter;
  float near_plane,far_plane,history_weight,previous_pre_exposure;
  float current_pre_exposure;uint reset,reversed,padding;
  float previous_near_plane,previous_far_plane;float2 padding2;
};
float3 temporal_ycocg(float3 c){return float3(dot(c,float3(0.25,0.5,0.25)),0.5*(c.r-c.b),dot(c,float3(-0.25,0.5,-0.25)));}
float3 temporal_rgb(float3 c){return float3(c.x+c.y-c.z,c.x+c.z,c.x-c.y-c.z);}
float temporal_view_depth(float z,float near_plane,float far_plane,bool reversed){z=reversed?1-z:z;return near_plane*far_plane/max(far_plane-z*(far_plane-near_plane),1.0e-8f);}
bool temporal_valid_depth(float z){return isfinite(z) && z>=0 && z<=1;}
bool temporal_depth_matches(float z,float reference,float near_plane,float far_plane,bool reversed){
 if(!temporal_valid_depth(z) || !temporal_valid_depth(reference))return false;
 float expected=temporal_view_depth(reference,near_plane,far_plane,reversed);
 return abs(temporal_view_depth(z,near_plane,far_plane,reversed)-expected)<=max(0.005f,0.02f*expected);
}
float4 temporal_finite(float4 c){return select(clamp(c,0.0f,65504.0f),float4(0),!isfinite(c));}
kernel void liberty_temporal_aa(texture2d<float> color [[texture(0)]],texture2d<float> depth [[texture(1)]],
 texture2d<float> motion [[texture(2)]],texture2d<float> reactive [[texture(3)]],
 texture2d<float> history [[texture(4)]],texture2d<float> history_depth [[texture(5)]],
 texture2d<float,access::write> result [[texture(6)]],texture2d<float,access::write> result_depth [[texture(7)]],
 texture2d<float> expected_previous_depth [[texture(8)]],
 constant TemporalConstants& c [[buffer(0)]],uint2 id [[thread_position_in_grid]]) {
 if(any(id>=c.extent))return;
 constexpr sampler linear_sampler(coord::normalized,address::clamp_to_edge,filter::linear);
 constexpr sampler nearest(coord::normalized,address::clamp_to_edge,filter::nearest);
 float2 uv=(float2(id)+0.5f)/float2(c.extent),jittered=uv+c.jitter/float2(c.extent);
 float4 current=temporal_finite(color.sample(linear_sampler,jittered));
 float z=depth.sample(nearest,jittered).r;
 result_depth.write(float4(temporal_valid_depth(z)?z:-1.0f),id);
 if(c.reset || !temporal_valid_depth(z)){result.write(current,id);return;}
 float2 reference_motion=motion.sample(nearest,jittered).xy;
 float reference_previous=expected_previous_depth.sample(nearest,jittered).r;
 if(!all(isfinite(reference_motion)) || !temporal_valid_depth(reference_previous)){result.write(current,id);return;}
 // Color reconstruction covers up to four raw samples. A nearest validity
 // sample can miss a new foreground contributor completely. Validate exactly
 // the nonzero color footprint, and interpolate motion/depth only within a
 // coherent surface. The half-pixel motion bound rejects mixed moving layers.
 float2 pixel=jittered*float2(c.extent)-0.5f,part=fract(pixel);
 int2 base=int2(floor(pixel));float2 velocity=0;float prior_z=0,reactive_value=0;
 for(int y=0;y<2;++y)for(int x=0;x<2;++x){
  float weight=(x?part.x:1-part.x)*(y?part.y:1-part.y);
  if(weight<=0)continue;
  uint2 tap=uint2(clamp(base+int2(x,y),int2(0),int2(c.extent)-1));
  float tap_z=depth.read(tap).r,tap_previous=expected_previous_depth.read(tap).r;
  float2 tap_motion=motion.read(tap).xy;float tap_reactive=reactive.read(tap).r;
  if(!all(isfinite(tap_motion)) || any(abs(tap_motion-reference_motion)>0.5f) ||
     !isfinite(tap_reactive) || tap_reactive>=1 ||
     !temporal_depth_matches(tap_z,z,c.near_plane,c.far_plane,c.reversed) ||
     !temporal_depth_matches(tap_previous,reference_previous,c.previous_near_plane,c.previous_far_plane,c.reversed)){
   result.write(current,id);return;
  }
  velocity+=tap_motion*weight;prior_z+=tap_previous*weight;
  reactive_value=max(reactive_value,tap_reactive);
 }
 float2 previous_uv=uv+velocity/float2(c.extent);
 if(any(previous_uv<0) || any(previous_uv>1)){result.write(current,id);return;}
 // Compare the expected previous position of this surface with depth from
 // that same frame, including every contributor to bilinear history color.
 // Current view-space Z is deliberately not part of this comparison.
 pixel=previous_uv*float2(c.extent)-0.5f;part=fract(pixel);base=int2(floor(pixel));
 for(int y=0;y<2;++y)for(int x=0;x<2;++x){
  float weight=(x?part.x:1-part.x)*(y?part.y:1-part.y);
  if(weight<=0)continue;
  uint2 tap=uint2(clamp(base+int2(x,y),int2(0),int2(c.extent)-1));
  if(!temporal_depth_matches(history_depth.read(tap).r,prior_z,c.previous_near_plane,c.previous_far_plane,c.reversed)){
   result.write(current,id);return;
  }
 }
 float3 mean=0,second=0,lower=INFINITY,upper=-INFINITY;
 // Compute extrema after transforming each sample into YCoCg, not by
 // transforming RGB extrema: chroma coefficients include negative terms.
 for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
  float3 value=temporal_ycocg(temporal_finite(color.sample(linear_sampler,jittered+float2(x,y)/float2(c.extent))).rgb);
  mean+=value;second+=value*value;lower=min(lower,value);upper=max(upper,value);
 }
 mean/=9;float3 deviation=sqrt(max(second/9-mean*mean,0.0f));
 lower=max(lower,mean-1.25f*deviation);upper=min(upper,mean+1.25f*deviation);
 float3 previous=temporal_ycocg(temporal_finite(history.sample(linear_sampler,previous_uv)).rgb*c.current_pre_exposure/c.previous_pre_exposure);
 float3 center=(lower+upper)*0.5f,extent=(upper-lower)*0.5f,delta=previous-center;
 float largest=max(max(abs(delta.x)/max(extent.x,1.0e-6f),abs(delta.y)/max(extent.y,1.0e-6f)),abs(delta.z)/max(extent.z,1.0e-6f));
 previous=center+delta/max(1.0f,largest);
 float weight=c.history_weight*(1-clamp(reactive_value,0.0f,1.0f));
 weight*=mix(1.0f,0.8f,clamp(length(velocity)/8,0.0f,1.0f));
 result.write(temporal_finite(float4(mix(current.rgb,temporal_rgb(previous),weight),current.a)),id);
}

kernel void liberty_temporal_ui_over(texture2d<float,access::read> scene [[texture(0)]],
 texture2d<float,access::read> overlay [[texture(1)]],texture2d<float,access::write> result [[texture(2)]],
 uint2 id [[thread_position_in_grid]]) {
 if(id.x>=result.get_width() || id.y>=result.get_height())return;
 float4 ui=temporal_finite(overlay.read(id));ui.a=clamp(ui.a,0.0f,1.0f);
 result.write(ui+temporal_finite(scene.read(id))*(1-ui.a),id);
}

kernel void liberty_temporal_ui_over_inplace(texture2d<float,access::read_write> image [[texture(0)]],
 texture2d<float,access::read> overlay [[texture(1)]],uint2 id [[thread_position_in_grid]]) {
 if(id.x>=image.get_width() || id.y>=image.get_height())return;
 float4 ui=temporal_finite(overlay.read(id));ui.a=clamp(ui.a,0.0f,1.0f);
 image.write(ui+temporal_finite(image.read(id))*(1-ui.a),id);
}

kernel void liberty_temporal_export_depth(depth2d<float,access::read> input [[texture(0)]],
 texture2d<float,access::write> output [[texture(1)]],uint2 id [[thread_position_in_grid]]) {
 if(id.x>=output.get_width()||id.y>=output.get_height())return;
 output.write(float4(input.read(id)),id);
}
kernel void liberty_temporal_copy_color(texture2d<float> input [[texture(0)]],
 texture2d<float,access::write> output [[texture(1)]],constant float2& offset [[buffer(0)]],uint2 id [[thread_position_in_grid]]) {
 if(id.x>=output.get_width()||id.y>=output.get_height())return;
 constexpr sampler s(coord::normalized,address::clamp_to_edge,filter::linear);
 float2 uv=(float2(id)+0.5f)/float2(output.get_width(),output.get_height())+offset/float2(input.get_width(),input.get_height());
 output.write(temporal_finite(input.sample(s,uv)),id);
}
kernel void liberty_temporal_affine_ui(texture2d<float> scene [[texture(0)]],texture2d<float> add [[texture(1)]],
 texture2d<float> transmit [[texture(2)]],texture2d<float,access::write> output [[texture(3)]],uint2 id [[thread_position_in_grid]]) {
 if(id.x>=output.get_width()||id.y>=output.get_height())return;
 constexpr sampler s(coord::normalized,address::clamp_to_edge,filter::linear);
 float2 uv=(float2(id)+0.5f)/float2(output.get_width(),output.get_height());
 output.write(temporal_finite(float4(scene.sample(s,uv).rgb*transmit.sample(s,uv).rgb+add.sample(s,uv).rgb,1)),id);
}

kernel void liberty_temporal_affine_ui_inplace(texture2d<float,access::read_write> image [[texture(0)]],
 texture2d<float> add [[texture(1)]],texture2d<float> transmit [[texture(2)]],uint2 id [[thread_position_in_grid]]) {
 if(id.x>=image.get_width()||id.y>=image.get_height())return;
 constexpr sampler s(coord::normalized,address::clamp_to_edge,filter::linear);
 float2 uv=(float2(id)+0.5f)/float2(image.get_width(),image.get_height());
 image.write(temporal_finite(float4(image.read(id).rgb*transmit.sample(s,uv).rgb+add.sample(s,uv).rgb,1)),id);
}

#include "temporal_exposure.metal"
#include "temporal_motion_coverage.metal"
