#version 460
// PS_GTACompositeMB, guest container hash 535ACDAB8AE84D82.
// Derived from the existing cached SPIR-V, not a different blur approximation.
// Adjacent regions sharing one unchanged predicate are one structured region.
// Sample positions, count, bias, masking, arithmetic order and output coverage
// remain unchanged at the source-operation level. No lower precision or downscale.

#if defined(GL_ARB_gpu_shader_int64)
#extension GL_ARB_gpu_shader_int64 : require
#else
#error No extension available for 64-bit integers.
#endif
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference_uvec2 : require
#extension GL_EXT_samplerless_texture_functions : require
#extension GL_EXT_spirv_intrinsics : require
#if defined(GL_EXT_control_flow_attributes)
#extension GL_EXT_control_flow_attributes : require
#define SPIRV_CROSS_FLATTEN [[flatten]]
#define SPIRV_CROSS_BRANCH [[dont_flatten]]
#define SPIRV_CROSS_UNROLL [[unroll]]
#define SPIRV_CROSS_LOOP [[dont_unroll]]
#else
#define SPIRV_CROSS_FLATTEN
#define SPIRV_CROSS_BRANCH
#define SPIRV_CROSS_UNROLL
#define SPIRV_CROSS_LOOP
#endif
#ifndef XENOS_RECOMP_LATE_FRAGMENT_TESTS
layout(early_fragment_tests) in;
#endif

layout(buffer_reference) buffer vec2Pointer;
layout(buffer_reference) buffer uintPointer;
layout(buffer_reference) buffer floatPointer;
layout(buffer_reference) buffer vec4Pointer;

layout(constant_id = 0) const uint _8 = 0u;
const uint _119 = (_8 & 2u);

layout(buffer_reference, buffer_reference_align = 4) buffer vec2Pointer
{
    vec2 value;
};

layout(buffer_reference, buffer_reference_align = 4) buffer uintPointer
{
    uint value;
};

layout(buffer_reference, buffer_reference_align = 4) buffer floatPointer
{
    float value;
};

layout(buffer_reference, buffer_reference_align = 16) buffer vec4Pointer
{
    vec4 value;
};

layout(push_constant, std430) uniform _9_10
{
    uint64_t _m0;
    uint64_t _m1;
    uint64_t _m2;
} _10;

layout(set = 0, binding = 0) uniform texture2D _12[];
layout(set = 4, binding = 0) uniform sampler _14[];

layout(location = 0) in vec4 _4;
layout(location = 1) in vec4 _5;
layout(location = 0) out vec4 _6;

spirv_instruction(set = "GLSL.std.450", id = 79) float spvNMin(float, float);
spirv_instruction(set = "GLSL.std.450", id = 79) vec2 spvNMin(vec2, vec2);
spirv_instruction(set = "GLSL.std.450", id = 79) vec3 spvNMin(vec3, vec3);
spirv_instruction(set = "GLSL.std.450", id = 79) vec4 spvNMin(vec4, vec4);
spirv_instruction(set = "GLSL.std.450", id = 80) float spvNMax(float, float);
spirv_instruction(set = "GLSL.std.450", id = 80) vec2 spvNMax(vec2, vec2);
spirv_instruction(set = "GLSL.std.450", id = 80) vec3 spvNMax(vec3, vec3);
spirv_instruction(set = "GLSL.std.450", id = 80) vec4 spvNMax(vec4, vec4);

layout(buffer_reference, buffer_reference_align = 8) readonly buffer uint64Pointer { uint64_t value; };

// FusionShaders rage_postfxPS11: gamma 2.2 -> PQ -> default Frostbite LUT.
// The flattened BGRA8 LUT is identical on Vulkan and Metal (no sRGB decode).
vec3 FusionLutTexel(uint64_t address, uvec3 p) {
  uint index = p.y * 1024u + p.z * 32u + p.x;
  uint bgra = uintPointer(address + uint64_t(index) * 4u).value;
  return vec3((bgra >> 16u) & 255u, (bgra >> 8u) & 255u, bgra & 255u) / 255.0;
}
vec3 FusionToneMap(vec3 color) {
  uint enabled = uintPointer(_10._m2 + 0x428ul).value;
  uint64_t address = uint64Pointer(_10._m2 + 0x420ul).value;
  if (enabled == 0u || address == 0ul) return color;
  vec3 linear = pow(abs(color), vec3(2.2));
  vec3 p = pow(linear * 0.01, vec3(0.159301757813));
  vec3 pq = clamp(pow((18.8515625 * p + 0.8359375) / (18.6875 * p + 1.0), vec3(78.84375)), vec3(0.0), vec3(1.0));
  vec3 position = pq * 31.0;
  uvec3 lo = uvec3(position);
  uvec3 hi = min(lo + 1u, 31u);
  vec3 f = fract(position);
  vec3 a = mix(FusionLutTexel(address, uvec3(lo.x, lo.y, lo.z)), FusionLutTexel(address, uvec3(hi.x, lo.y, lo.z)), f.x);
  vec3 b = mix(FusionLutTexel(address, uvec3(lo.x, hi.y, lo.z)), FusionLutTexel(address, uvec3(hi.x, hi.y, lo.z)), f.x);
  vec3 c = mix(FusionLutTexel(address, uvec3(lo.x, lo.y, hi.z)), FusionLutTexel(address, uvec3(hi.x, lo.y, hi.z)), f.x);
  vec3 d = mix(FusionLutTexel(address, uvec3(lo.x, hi.y, hi.z)), FusionLutTexel(address, uvec3(hi.x, hi.y, hi.z)), f.x);
  return mix(mix(a, b, f.y), mix(c, d, f.y), f.z);
}


vec3 FusionBloomThreshold(vec3 exposed, float threshold) {
  float brightness = sqrt(dot(exposed * exposed, vec3(0.212500006, 0.715399981, 0.0720999986)));
  return exposed * (brightness > 0.0 ? max((brightness - threshold) / brightness, 0.0) : 0.0);
}
void main()
{
    vec4 _163 = vec4(0.0);
    vec4 _168 = vec4(0.25, 0.125, 0.0, 0.0);
    vec4 _169 = vec4(256.0, 0.5, 0.875, -1.0);
    vec4 _170 = vec4(0.0, 1.0, 0.375, 0.5);
    vec4 _171 = vec4(8.0, 4.0, 2.0, 256.0);
    vec4 _172 = _4;
    vec4 _174 = vec4(0.0);
    vec4 _176 = vec4(0.0);
    vec4 _179 = vec4(0.0);
    vec4 _180 = vec4(0.0);
    vec4 _181 = vec4(0.0);
    vec4 _182 = vec4(0.0);
    uintPointer _198 = uintPointer(_10._m2 + 24ul);
    uintPointer _201 = uintPointer(_10._m2 + 440ul);
    floatPointer _205 = floatPointer(_10._m2 + 776ul);
    _172.w = texture(sampler2D(_12[_198.value], _14[_201.value]), _4.xy, _205.value).z;
    vec4 _221 = _172;
    _180.z = texture(sampler2D(_12[uintPointer(_10._m2 + 4ul).value], _14[uintPointer(_10._m2 + 420ul).value]), _221.xy, floatPointer(_10._m2 + 756ul).value).x;
    _174.x = texture(sampler2D(_12[uintPointer(_10._m2).value], _14[uintPointer(_10._m2 + 416ul).value]), _221.xy, floatPointer(_10._m2 + 752ul).value).w;
    uintPointer _251 = uintPointer(_10._m2 + 8ul);
    uintPointer _254 = uintPointer(_10._m2 + 424ul);
    floatPointer _257 = floatPointer(_10._m2 + 760ul);
    uvec2 _263 = uvec2(textureSize(_12[_251.value], 0));
    uvec2 _167;
    _167.x = _263.x;
    _167.y = _263.y;
    vec4 _273 = texture(sampler2D(_12[_251.value], _14[_254.value]), _221.xy + (vec2(-0.5, -1.5) / vec2(_167)), _257.value);
    vec4 _178 = vec4(_273.x, _273.y, _273.z, vec4(0.0).w);
    vec4 _283 = texture(sampler2D(_12[_251.value], _14[_254.value]), _221.xy, _257.value);
    vec4 _284 = _174;
    vec4 _285 = vec4(_284.x, _283.x, _283.y, _283.z);
    _174 = _285;
    uvec2 _293 = uvec2(textureSize(_12[_251.value], 0));
    uvec2 _166;
    _166.x = _293.x;
    _166.y = _293.y;
    vec4 _303 = texture(sampler2D(_12[_251.value], _14[_254.value]), _221.xy + (vec2(1.5, -0.5) / vec2(_166)), _257.value);
    uvec2 _312 = uvec2(textureSize(_12[_251.value], 0));
    uvec2 _165;
    _165.x = _312.x;
    _165.y = _312.y;
    vec4 _322 = texture(sampler2D(_12[_251.value], _14[_254.value]), _221.xy + (vec2(0.5, 1.5) / vec2(_165)), _257.value);
    uvec2 _331 = uvec2(textureSize(_12[_251.value], 0));
    uvec2 _164;
    _164.x = _331.x;
    _164.y = _331.y;
    vec4 _341 = texture(sampler2D(_12[_251.value], _14[_254.value]), _221.xy + (vec2(-1.5, 0.5) / vec2(_164)), _257.value);
    vec4 _175 = vec4(_341.x, _341.y, _341.z, vec4(0.0).w);
    vec4 _357 = texture(sampler2D(_12[uintPointer(_10._m2 + 12ul).value], _14[uintPointer(_10._m2 + 428ul).value]), _221.xy, floatPointer(_10._m2 + 764ul).value);
    vec4 _173 = vec4(_5.x, _357.x, _357.y, _357.z);
    vec2 _361 = (_221.yx * vec2(2.0)) + vec2(-1.0);
    _180 = vec4(_361.x, _361.y, _180.z, _180.w);
    vec4Pointer _370 = vec4Pointer(_10._m1 + 3344ul);
    _181.w = (-_180.x) * _370.value.w;
    _179.z = dot(_341.zxy, vec3(0.07209999859333038330078125, 0.2125000059604644775390625, 0.7153999805450439453125));
    _176.x = dot(_322.zxy, vec3(0.07209999859333038330078125, 0.2125000059604644775390625, 0.7153999805450439453125));
    _176.y = dot(_303.zxy, vec3(0.07209999859333038330078125, 0.2125000059604644775390625, 0.7153999805450439453125));
    _176.z = dot(_283.zxy, vec3(0.07209999859333038330078125, 0.2125000059604644775390625, 0.7153999805450439453125));
    _176.w = dot(_273.zxy, vec3(0.07209999859333038330078125, 0.2125000059604644775390625, 0.7153999805450439453125));
    vec4 _390 = _285 * vec4(256.0, 0.5, 0.5, 0.5);
    vec4 _177 = _390;
    _174.w = float((-abs(_172.x)) > 0.0);
    vec4 _398 = _179;
    vec4 _399 = _176;
    vec4 _183 = vec4(greaterThanEqual(vec4(8.0, 4.0, 2.0, 256.0), _390.xxxx));
    _172.z = float((-_177.x) >= 0.0);
    _182.w = _172.z + (-_183.z);
    _177.x = _370.value.y * _370.value.x;
    _180.w = dot(vec4(_398.z, _399.x, _399.w, _399.y).yzwx, vec4(0.25));
    _182.x = _183.x - _183.w;
    _176 = (-_180.wwww) + _399;
    _182.y = _183.z - _183.y;
    vec3 _442 = _180.yzz * _370.value.zyx;
    vec4 _443 = _181;
    vec4 _444 = vec4(_442.x, _442.y, _442.z, _443.w);
    _181 = _444;
    _182.z = _183.y - _183.x;
    vec4 _449 = _182;
    _172.z = dot(_449.xzyw, vec4(-0.5));
    _174.x = _181.y - _181.z;
    vec4 _458 = _449.wyxz * vec4(-0.5);
    _182 = _458;
    _173.x = _170.w + _172.z;
    vec4 _467 = _177;
    vec3 _471 = _303.zyx;
    _172.z = _174.x + _370.value.x;
    _174.y = clamp(1.0 / _173.x, -3.4028234663852885981170418348452e+38, 3.4028234663852885981170418348452e+38);
    _176.z *= _176.z;
    _174.x = clamp(1.0 / _172.z, -3.4028234663852885981170418348452e+38, 3.4028234663852885981170418348452e+38);
    vec3 _490 = _322.zyx;
    vec3 _498 = (_458.zzz * _341.zyx) + ((_458.www * _490) + ((_458.yyy * _471) + ((_458.xxx * _273.zyx) + _467.wzy).xyz).xyz).xyz;
    vec4 _500 = _174;
    vec4 _502 = vec4(_467.x, _498.x, _498.y, _498.z) * _500.xyyy;
    _177 = _502;
    vec4Pointer _504 = vec4Pointer(_10._m1 + 3376ul);
    _179.w = _504.value.z - _504.value.y;
    vec4Pointer _513 = vec4Pointer(_10._m1 + 3536ul);
    _175.w = float(_172.w >= _513.value.x);
    _172.z = spvNMax(_177.x, _177.x);
    vec2 _524 = _444.xw * _502.xx;
    _174 = vec4(_524.x, _500.y, _524.y, _500.w);
    vec4Pointer _527 = vec4Pointer(_10._m1 + 3360ul);
    _172.z = (-_527.value.w) - (-_172.z);
    _172.z = ((-_527.value.y) * _170.w) + _172.z;
    _173.x = spvNMax(_172.z, _170.x);
    vec4Pointer _566 = vec4Pointer(_10._m1 + 3328ul);
    _174.z = _566.value.x * _173.w;
    vec3 _581 = (_174.xxx * vec4Pointer(_10._m1 + 3408ul).value.zxy) + ((_524.yyy * vec4Pointer(_10._m1 + 3424ul).value.zxy) + (((-_502.xxx) * vec4Pointer(_10._m1 + 3440ul).value.zxy) + vec4Pointer(_10._m1 + 3456ul).value.zxy).xyz).xyz;
    _181 = vec4(_581.x, _581.y, _581.z, _443.w);
    _174.x = (-_181.x) * _370.value.z;
    _172.w = clamp(1.0 / _527.value.z, -3.4028234663852885981170418348452e+38, 3.4028234663852885981170418348452e+38);
    _172.z = _181.x * _370.value.w;
    _179.x = clamp(1.0 / _174.x, -3.4028234663852885981170418348452e+38, 3.4028234663852885981170418348452e+38);
    _181.w = _173.x * _172.w;
    _179.y = clamp(1.0 / _172.z, -3.4028234663852885981170418348452e+38, 3.4028234663852885981170418348452e+38);
    vec4 _610 = _179;
    vec3 _612 = _181.yzw * _610.xyw;
    vec4 _613 = vec4(_612.x, _612.y, _610.z, _612.z);
    _179 = _613;
    _174.y = _566.value.x * _173.z;
    _172.z = _179.w + _504.value.y;
    _174.x = _566.value.x * _173.y;
    _173.y = dot(_176.xwy, _176.xwy);
    _172.z = spvNMax(_172.z, _172.z);
    vec3 _637 = (-_180.yxw) + _613.xyz;
    _179 = vec4(_612.x, _637.x, _637.y, _637.z);
    _172.w = spvNMax(_504.value.z, _504.value.z);
    vec2 _649 = _637.xy * vec4Pointer(_10._m1 + 3392ul).value.xx;
    _173 = vec4(_649.x, _173.y, _173.z, _649.y);
    _179.x = spvNMin(_172.z, _172.w);
    vec2 _657 = _179.xw * _179.xw;
    _172 = vec4(_172.x, _172.y, _657.x, _657.y);
    _176.x = _168.y * _173.x;
    _173.y += _172.w;
    _176.y = _168.y * _173.w;
    _173.y = float(_176.z >= _173.y);
    _173.z = _170.y - _172.z;
    _172.z = (_173.z * _173.y) + _172.z;
    _173.y = _172.z * _168.x;
    _172.w = _170.y - _172.z;
    vec3 _694 = _173.yyy * _175.zyx;
    _175 = vec4(_694.x, _694.y, _694.z, _175.w);
    float _696 = _175.w;
    bool _697 = _696 == 0.0;
    vec3 _718 = (_177.wzy * _172.www) + ((_173.yyy * _178.zyx) + ((_173.yyy * _471) + ((_173.yyy * _490) + _175.xyz).xyz).xyz).zyx;
    _175 = vec4(_718.x, _718.y, _718.z, _175.w);
    if (floatPointer(_10._m2 + 596ul).value != 0.0)
        _175.xyz = texture(sampler2D(_12[_251.value], _14[_254.value]), _4.xy, _257.value).xyz;
    vec4 _819;
    vec4 _847;
    vec4 _862;
    if (_697)
    {
        vec2 _724 = _175.xy * vec2(8.0);
        _173 = vec4(_173.x, _724.x, _724.y, _173.w);
    
        vec2 _734 = (_172.xy * vec2(58.16400146484375, 47.130001068115234375)) + _173.yz;
        _173 = vec4(_173.x, _734.x, _734.y, _173.w);
    
        _173.y = texture(sampler2D(_12[uintPointer(_10._m2 + 20ul).value], _14[uintPointer(_10._m2 + 436ul).value]), _173.yz, floatPointer(_10._m2 + 772ul).value).x;
    
        _172.w = (-_170.w) - (-_173.y);
    
        vec2 _767 = _176.xy + _176.xy;
        _177 = vec4(_177.x, _177.y, _767.x, _767.y);
        _177.x = _169.z * _173.x;
    
        vec2 _782 = (_176.xy * _172.ww) + _172.xy;
        _173 = vec4(_173.x, _782.x, _782.y, _173.w);
    
        vec2 _791 = _173.yz + _176.xy;
        _172 = vec4(_791.x, _791.y, _172.z, _172.w);
        _177.y = _169.z * _173.w;
    
        _183 = _173.yzyz + _177;
    
        _179 = (_173.xxww * vec4(0.5, 0.375, 0.375, 0.5)) + _173.yyzz;
    
        _819 = (_173.xwxw * vec4(0.625, 0.625, 0.75, 0.75)) + _173.yzyz;
    
        vec4 _831 = texture(sampler2D(_12[_251.value], _14[_254.value]), _819.zw, _257.value);
        _177 = vec4(_831.x, _831.y, _831.z, _177.w);
    
        vec4 _845 = texture(sampler2D(_12[_251.value], _14[_254.value]), _819.xy, _257.value);
        _847 = vec4(_845.x, _845.y, _845.z, vec4(0.0).w);
    
        vec4 _860 = texture(sampler2D(_12[_251.value], _14[_254.value]), _179.xw, _257.value);
        _862 = vec4(_860.x, _860.y, _860.z, vec4(0.0).w);
    
        vec4 _875 = texture(sampler2D(_12[_251.value], _14[_254.value]), _179.yz, _257.value);
        _180 = vec4(_875.x, _875.y, _875.z, _180.w);
    
        vec4 _890 = texture(sampler2D(_12[_251.value], _14[_254.value]), _183.zw, _257.value);
        _181 = vec4(_890.x, _890.y, _890.z, _181.w);
    
        vec4 _905 = texture(sampler2D(_12[_251.value], _14[_254.value]), _172.xy, _257.value);
        _176 = vec4(_176.x, _905.x, _905.y, _905.z);
    
        vec4 _920 = texture(sampler2D(_12[_251.value], _14[_254.value]), _183.xy, _257.value);
        _182 = vec4(_920.x, _920.y, _920.z, _182.w);
    
        _178.x = texture(sampler2D(_12[_198.value], _14[_201.value]), _183.xy, _205.value).z;
    
        _178.y = texture(sampler2D(_12[_198.value], _14[_201.value]), _819.xy, _205.value).z;
    
        _178.z = texture(sampler2D(_12[_198.value], _14[_201.value]), _819.zw, _205.value).z;
    
        _179.x = texture(sampler2D(_12[_198.value], _14[_201.value]), _179.xw, _205.value).z;
    
        _179.y = texture(sampler2D(_12[_198.value], _14[_201.value]), _179.yz, _205.value).z;
    
        _179.z = texture(sampler2D(_12[_198.value], _14[_201.value]), _172.xy, _205.value).z;
    
        _179.w = texture(sampler2D(_12[_198.value], _14[_201.value]), _183.zw, _205.value).z;
    
        vec2 _1030 = _173.xw * vec4Pointer(_10._m1 + 704ul).value.xy;
        _172 = vec4(_1030.x, _1030.y, _172.z, _172.w);
    
        _172.x = dot(_172.xy, _172.xy) + _170.x;
    
        _179 = vec4(greaterThanEqual(_179, _513.value.xxxx));
    
        vec3 _1054 = vec3(greaterThanEqual(_178.xyz, _513.value.xxx));
        _173 = vec4(_173.x, _1054.x, _1054.y, _1054.z);
    
        _176.x = dot(-_173.zyw, vec3(1.0));
    
        _173.x = dot(-_179.zwyx, vec4(1.0));
    
        vec3 _1073 = vec3(1.0) - _173.zyw;
        _178 = vec4(_1073.x, _1073.y, _1073.z, _178.w);
    
        _179 = vec4(1.0) - _179.ywxz;
        _172.x = sqrt(abs(_172.x));
    
        vec3 _1090 = _178.yyy * _182.zyx;
        _173 = vec4(_173.x, _1090.x, _1090.y, _1090.z);
        _172.y = _170.y + _172.z;
    
        vec3 _1106 = (_179.www * _176.yzw) + _175.xyz;
        _176 = vec4(_176.x, _1106.x, _1106.y, _1106.z);
    
        vec3 _1117 = (_179.yyy * _181.xyz) + _176.yzw;
        _176 = vec4(_176.x, _1117.x, _1117.y, _1117.z);
    
        vec3 _1128 = (_179.xxx * _180.xyz) + _176.yzw;
        _176 = vec4(_176.x, _1128.x, _1128.y, _1128.z);
    
        vec3 _1138 = (_179.zzz * _862.xyz) + _176.yzw;
        _176 = vec4(_176.x, _1138.x, _1138.y, _1138.z);
    
        vec3 _1148 = (_178.xxx * _847.xyz) + _176.yzw;
        _176 = vec4(_176.x, _1148.x, _1148.y, _1148.z);
    
        vec3 _1159 = (_178.zzz * _177.xyz) + _176.yzw;
        _176 = vec4(_176.x, _1159.x, _1159.y, _1159.z);
    
        _173 = _176 + _173.xwzy;
        _172.z = _170.w * _172.x;
    
        _172.x = _173.x + _171.x;
        _172.y = clamp(1.0 / _172.y, -3.4028234663852885981170418348452e+38, 3.4028234663852885981170418348452e+38);
    
        _172.w = clamp(_172.z * _172.y, 0.0, 1.0);
        _172.x = clamp(1.0 / _172.x, -3.4028234663852885981170418348452e+38, 3.4028234663852885981170418348452e+38);
    
        vec3 _1200 = (_173.yzw * _172.xxx) + (-_175.xyz);
        _172 = vec4(_1200.x, _1200.y, _1200.z, _172.w);
    
        vec3 _1210 = (_172.www * _172.xyz) + _175.xyz;
        _175 = vec4(_1210.x, _1210.y, _1210.z, _175.w);
        }
    _172.x = texture(sampler2D(_12[uintPointer(_10._m2 + 16ul).value], _14[uintPointer(_10._m2 + 432ul).value]), _174.ww, floatPointer(_10._m2 + 768ul).value).x;
    _172.x = clamp(1.0 / _172.x, -3.4028234663852885981170418348452e+38, 3.4028234663852885981170418348452e+38);
    vec4Pointer _1234 = vec4Pointer(_10._m1 + 3472ul);
    _172.w = _1234.value.y * _172.x;
    vec4Pointer _1240 = vec4Pointer(_10._m1 + 3488ul);
    _176.x = _1240.value.z + (-_170.y);
    _172.x = clamp(1.0 / _172.w, -3.4028234663852885981170418348452e+38, 3.4028234663852885981170418348452e+38);
    vec4 _1249 = _172;
    vec3 fusion_bloom = uintPointer(_10._m2 + 0x428ul).value != 0u
        ? FusionBloomThreshold(_174.xyz, _1249.x * _1234.value.x)
        : spvNMax((((-_1249.xxx) * _1234.value.xxx) + _174.xyz).xyz, vec3(0.0));
    vec3 _1271 = (_175.xyz * _566.value.xxx) + ((fusion_bloom * _1234.value.zzz).xyz * vec3(0.25)).xyz;
    vec3 _1274 = _1249.www * _1271.xyz;
    _172 = vec4(_1271.x, _1274.x, _1274.y, _1274.z);
    _172.x = dot(_1274.zxy, vec3(0.07209999859333038330078125, 0.2125000059604644775390625, 0.7153999805450439453125));
    vec4 _1278 = _172;
    vec4Pointer _1284 = vec4Pointer(_10._m1 + 3520ul);
    _172.y = _1284.value.w * _172.x;
    vec4 _1290 = _172;
    vec3 _1301 = (((-_1278.xxx) + _1278.yzw).xyz * _1240.value.xxx) + _1290.xxx;
    _174 = vec4(_1301.x, _1301.y, _1301.z, _174.w);
    vec2 _1305 = clamp(spvNMax(_1290.yx, _1290.yx), vec2(0.0), vec2(1.0));
    vec4 _1306 = _176;
    vec4 _1307 = vec4(_1306.x, _1305.x, _1305.y, _1306.w);
    _176 = _1307;
    vec3 _1309 = (_1290.yyy * _1284.value.xyz).xyz;
    vec3 _1311 = _1301.xyz + (-_1309);
    _172 = vec4(_1290.x, _1311.x, _1311.y, _1311.z);
    _172.x = clamp(log2(_176.z), -3.4028234663852885981170418348452e+38, 3.4028234663852885981170418348452e+38);
    vec4 _1318 = _1307.xyyy * _172;
    vec3 _1326 = (_1309 + _1318.yzw).xyz * vec4Pointer(_10._m1 + 3504ul).value.xyz;
    _173 = vec4(_1326.x, _1326.y, _1326.z, _173.w);
    vec3 _1328 = _1326.xyz;
    vec3 _1329 = _1328 + _1328;
    _172 = vec4(_1318.x, _1329.x, _1329.y, _1329.z);
    _172.x = exp2(_172.x);
    vec4 _1336 = _172.yzww * _172.xxxx;
    _163 = vec4(_1336.x, _1336.y, _1336.z, _163.w);
    _163.w = 1.0;
    SPIRV_CROSS_BRANCH
    if (_119 != 0u)
    {
        uint _1344 = (_8 >> 8u) & 7u;
        bool _1353;
        if (_1344 == 1u)
        {
            _1353 = _163.w < floatPointer(_10._m2 + 580ul).value;
        }
        else
        {
            _1353 = false;
        }
        bool _1366;
        if (!_1353)
        {
            bool _1365;
            if (_1344 == 2u)
            {
                _1365 = _163.w == floatPointer(_10._m2 + 580ul).value;
            }
            else
            {
                _1365 = false;
            }
            _1366 = _1365;
        }
        else
        {
            _1366 = true;
        }
        bool _1379;
        if (!_1366)
        {
            bool _1378;
            if (_1344 == 3u)
            {
                _1378 = _163.w <= floatPointer(_10._m2 + 580ul).value;
            }
            else
            {
                _1378 = false;
            }
            _1379 = _1378;
        }
        else
        {
            _1379 = true;
        }
        bool _1392;
        if (!_1379)
        {
            bool _1391;
            if (_1344 == 4u)
            {
                _1391 = _163.w > floatPointer(_10._m2 + 580ul).value;
            }
            else
            {
                _1391 = false;
            }
            _1392 = _1391;
        }
        else
        {
            _1392 = true;
        }
        bool _1414;
        if (!_1392)
        {
            bool _1413;
            if (_1344 == 5u)
            {
                floatPointer _1401 = floatPointer(_10._m2 + 580ul);
                bool _1412;
                if (!any(isnan(vec2(_163.w, _1401.value))))
                {
                    _1412 = _163.w != _1401.value;
                }
                else
                {
                    _1412 = true;
                }
                _1413 = _1412;
            }
            else
            {
                _1413 = false;
            }
            _1414 = _1413;
        }
        else
        {
            _1414 = true;
        }
        bool _1427;
        if (!_1414)
        {
            bool _1426;
            if (_1344 == 6u)
            {
                _1426 = _163.w >= floatPointer(_10._m2 + 580ul).value;
            }
            else
            {
                _1426 = false;
            }
            _1427 = _1426;
        }
        else
        {
            _1427 = true;
        }
        bool _1433;
        if (!_1427)
        {
            _1433 = (_1344 == 7u) ? true : false;
        }
        else
        {
            _1433 = true;
        }
        if ((_1433 ? 1.0 : (-1.0)) < 0.0)
        {
            discard;
        }
    }
    vec2 _184 = ((gl_FragCoord.xy - vec2(0.5)) * vec2Pointer(_10._m2 + 744ul).value) + vec2(0.5);
    uintPointer _1440 = uintPointer(_10._m2 + 736ul);
    uintPointer _1443 = uintPointer(_10._m2 + 740ul);
    uint _1516;
    do
    {
        if ((_1440.value & 256u) == 0u)
        {
            _1516 = 4294967295u;
            break;
        }
        float _1465 = float((_1440.value >> ((((uint(_184.x) & 1u) | ((uint(_184.y) & 1u) << 1u)) << 1u) & 31u)) & 3u);
        uint _1515;
        if (_1443.value == 1u)
        {
            _1515 = uint(_163.w >= (1.0 - (_1465 * 0.25)));
        }
        else
        {
            uint _1510;
            if (_1443.value == 2u)
            {
                float _1500 = _1465 * 0.125;
                uint _1503 = (_163.w >= (0.5 - _1500)) ? 2u : 0u;
                uint _1509;
                if (_163.w >= (1.0 - _1500))
                {
                    _1509 = _1503 | 1u;
                }
                else
                {
                    _1509 = _1503;
                }
                _1510 = _1509;
            }
            else
            {
                uint _1499;
                if (_1443.value == 4u)
                {
                    float _1478 = _1465 * 0.0625;
                    uint _1481 = uint(_163.w >= (0.75 - _1478));
                    uint _1487;
                    if (_163.w >= (0.25 - _1478))
                    {
                        _1487 = _1481 | 4u;
                    }
                    else
                    {
                        _1487 = _1481;
                    }
                    uint _1493;
                    if (_163.w >= (0.5 - _1478))
                    {
                        _1493 = _1487 | 2u;
                    }
                    else
                    {
                        _1493 = _1487;
                    }
                    if (_163.w >= (1.0 - _1478))
                    {
                        _1499 = _1493 | 8u;
                    }
                    else
                    {
                        _1499 = _1493;
                    }
                }
                else
                {
                    _1516 = 4294967295u;
                    break;
                }
                _1510 = _1499;
            }
            _1515 = _1510;
        }
        _1516 = _1515;
        break;
    } while(false);
    _6 = vec4(FusionToneMap(_163.rgb), _163.a);
    gl_SampleMask[0u] = int(_1516);
}
