// Xbox composite adapter. The original fused result is the fallback.
#include "../xenos_shader_common.h"
#include "split_composite.hlsli"

#ifdef __spirv__

#define AdapLumSampler_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 16)
#define AdapLumSampler_Texture2DArrayDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 120)
#define AdapLumSampler_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 224)
#define AdapLumSampler_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 328)
#define AdapLumSampler_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 432)
#define BloomSampler_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 12)
#define BloomSampler_Texture2DArrayDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 116)
#define BloomSampler_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 220)
#define BloomSampler_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 324)
#define BloomSampler_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 428)
#define ColorCorrect vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3424, 0x10)
#define ColorShift vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3440, 0x10)
#define Exposure vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3328, 0x10)
#define GBufferTextureSampler2_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 0)
#define GBufferTextureSampler2_Texture2DArrayDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 104)
#define GBufferTextureSampler2_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 208)
#define GBufferTextureSampler2_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 312)
#define GBufferTextureSampler2_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 416)
#define GBufferTextureSampler3_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 4)
#define GBufferTextureSampler3_Texture2DArrayDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 108)
#define GBufferTextureSampler3_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 212)
#define GBufferTextureSampler3_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 316)
#define GBufferTextureSampler3_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 420)
#define HDRSampler_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 8)
#define HDRSampler_Texture2DArrayDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 112)
#define HDRSampler_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 216)
#define HDRSampler_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 320)
#define HDRSampler_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 424)
#define ToneMapParams vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3392, 0x10)
#define deSatContrastGamma vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3408, 0x10)
#define dofBlur vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3376, 0x10)
#define dofDist vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3360, 0x10)
#define dofProj vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3344, 0x10)

#elif defined(__air__)

#define AdapLumSampler_Texture2DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 16)))
#define AdapLumSampler_Texture2DArrayDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 120)))
#define AdapLumSampler_Texture3DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 224)))
#define AdapLumSampler_TextureCubeDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 328)))
#define AdapLumSampler_SamplerDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 432)))
#define BloomSampler_Texture2DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 12)))
#define BloomSampler_Texture2DArrayDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 116)))
#define BloomSampler_Texture3DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 220)))
#define BloomSampler_TextureCubeDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 324)))
#define BloomSampler_SamplerDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 428)))
#define ColorCorrect (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 3424)))
#define ColorShift (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 3440)))
#define Exposure (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 3328)))
#define GBufferTextureSampler2_Texture2DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 0)))
#define GBufferTextureSampler2_Texture2DArrayDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 104)))
#define GBufferTextureSampler2_Texture3DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 208)))
#define GBufferTextureSampler2_TextureCubeDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 312)))
#define GBufferTextureSampler2_SamplerDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 416)))
#define GBufferTextureSampler3_Texture2DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 4)))
#define GBufferTextureSampler3_Texture2DArrayDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 108)))
#define GBufferTextureSampler3_Texture3DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 212)))
#define GBufferTextureSampler3_TextureCubeDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 316)))
#define GBufferTextureSampler3_SamplerDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 420)))
#define HDRSampler_Texture2DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 8)))
#define HDRSampler_Texture2DArrayDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 112)))
#define HDRSampler_Texture3DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 216)))
#define HDRSampler_TextureCubeDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 320)))
#define HDRSampler_SamplerDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 424)))
#define ToneMapParams (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 3392)))
#define deSatContrastGamma (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 3408)))
#define dofBlur (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 3376)))
#define dofDist (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 3360)))
#define dofProj (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 3344)))

#else

cbuffer PixelShaderConstants : register(b1, space4)
{
	float4 ColorCorrect : packoffset(c214);
	float4 ColorShift : packoffset(c215);
	float4 Exposure : packoffset(c208);
	float4 ToneMapParams : packoffset(c212);
	float4 deSatContrastGamma : packoffset(c213);
	float4 dofBlur : packoffset(c211);
	float4 dofDist : packoffset(c210);
	float4 dofProj : packoffset(c209);
};

cbuffer SharedConstants : register(b2, space4)
{
	uint AdapLumSampler_Texture2DDescriptorIndex : packoffset(c1.x);
	uint AdapLumSampler_Texture2DArrayDescriptorIndex : packoffset(c7.z);
	uint AdapLumSampler_Texture3DDescriptorIndex : packoffset(c14.x);
	uint AdapLumSampler_TextureCubeDescriptorIndex : packoffset(c20.z);
	uint AdapLumSampler_SamplerDescriptorIndex : packoffset(c27.x);
	uint BloomSampler_Texture2DDescriptorIndex : packoffset(c0.w);
	uint BloomSampler_Texture2DArrayDescriptorIndex : packoffset(c7.y);
	uint BloomSampler_Texture3DDescriptorIndex : packoffset(c13.w);
	uint BloomSampler_TextureCubeDescriptorIndex : packoffset(c20.y);
	uint BloomSampler_SamplerDescriptorIndex : packoffset(c26.w);
	uint GBufferTextureSampler2_Texture2DDescriptorIndex : packoffset(c0.x);
	uint GBufferTextureSampler2_Texture2DArrayDescriptorIndex : packoffset(c6.z);
	uint GBufferTextureSampler2_Texture3DDescriptorIndex : packoffset(c13.x);
	uint GBufferTextureSampler2_TextureCubeDescriptorIndex : packoffset(c19.z);
	uint GBufferTextureSampler2_SamplerDescriptorIndex : packoffset(c26.x);
	uint GBufferTextureSampler3_Texture2DDescriptorIndex : packoffset(c0.y);
	uint GBufferTextureSampler3_Texture2DArrayDescriptorIndex : packoffset(c6.w);
	uint GBufferTextureSampler3_Texture3DDescriptorIndex : packoffset(c13.y);
	uint GBufferTextureSampler3_TextureCubeDescriptorIndex : packoffset(c19.w);
	uint GBufferTextureSampler3_SamplerDescriptorIndex : packoffset(c26.y);
	uint HDRSampler_Texture2DDescriptorIndex : packoffset(c0.z);
	uint HDRSampler_Texture2DArrayDescriptorIndex : packoffset(c7.x);
	uint HDRSampler_Texture3DDescriptorIndex : packoffset(c13.z);
	uint HDRSampler_TextureCubeDescriptorIndex : packoffset(c20.x);
	uint HDRSampler_SamplerDescriptorIndex : packoffset(c26.z);
	DEFINE_SHARED_CONSTANTS();
};

#endif

struct Interpolators
{
#ifdef __air__
	float4 iPos [[position]];
	float4 iTexCoord0 [[user(TEXCOORD0)]];
	float4 iTexCoord1 [[user(TEXCOORD1)]];
	float4 iTexCoord2 [[user(TEXCOORD2)]];
	float4 iTexCoord3 [[user(TEXCOORD3)]];
	float4 iTexCoord4 [[user(TEXCOORD4)]];
	float4 iTexCoord5 [[user(TEXCOORD5)]];
	float4 iTexCoord6 [[user(TEXCOORD6)]];
	float4 iTexCoord7 [[user(TEXCOORD7)]];
	float4 iTexCoord8 [[user(TEXCOORD8)]];
	float4 iTexCoord9 [[user(TEXCOORD9)]];
	float4 iTexCoord10 [[user(TEXCOORD10)]];
	float4 iTexCoord11 [[user(TEXCOORD11)]];
	float4 iTexCoord12 [[user(TEXCOORD12)]];
	float4 iTexCoord13 [[user(TEXCOORD13)]];
	float4 iTexCoord14 [[user(TEXCOORD14)]];
	float4 iTexCoord15 [[user(TEXCOORD15)]];
	float4 iColor0 [[user(COLOR0)]];
	float4 iColor1 [[user(COLOR1)]];
#else
	float4 iPos : SV_Position;
	float4 iTexCoord0 : TEXCOORD0;
	float4 iTexCoord1 : TEXCOORD1;
	float4 iTexCoord2 : TEXCOORD2;
	float4 iTexCoord3 : TEXCOORD3;
	float4 iTexCoord4 : TEXCOORD4;
	float4 iTexCoord5 : TEXCOORD5;
	float4 iTexCoord6 : TEXCOORD6;
	float4 iTexCoord7 : TEXCOORD7;
	float4 iTexCoord8 : TEXCOORD8;
	float4 iTexCoord9 : TEXCOORD9;
	float4 iTexCoord10 : TEXCOORD10;
	float4 iTexCoord11 : TEXCOORD11;
	float4 iTexCoord12 : TEXCOORD12;
	float4 iTexCoord13 : TEXCOORD13;
	float4 iTexCoord14 : TEXCOORD14;
	float4 iTexCoord15 : TEXCOORD15;
	float4 iColor0 : COLOR0;
	float4 iColor1 : COLOR1;
#endif
};
struct PixelShaderOutput
{
#ifdef __air__
	float4 oC0 [[color(0)]];
#else
	float4 oC0 : SV_Target0;
#ifdef __spirv__
	uint oMask : SV_Coverage;
#endif
#endif
};
#ifdef __air__
[[fragment]]
[[early_fragment_tests]]
#else
#if !defined(__spirv__)
[shader("pixel")]
#endif
#include "../fusion_tone.hlsli"

#ifndef XENOS_RECOMP_LATE_FRAGMENT_TESTS
[earlydepthstencil]
#endif
#endif
PixelShaderOutput shaderMain(
#ifdef __air__
	Interpolators input [[stage_in]],
	bool iFace [[front_facing]],
	constant Texture2DDescriptorHeap* g_Texture2DDescriptorHeap [[buffer(0)]],
	constant Texture2DArrayDescriptorHeap* g_Texture2DArrayDescriptorHeap [[buffer(1)]],
	constant Texture3DDescriptorHeap* g_Texture3DDescriptorHeap [[buffer(2)]],
	constant TextureCubeDescriptorHeap* g_TextureCubeDescriptorHeap [[buffer(3)]],
	constant SamplerDescriptorHeap* g_SamplerDescriptorHeap [[buffer(4)]],
	constant PushConstants& g_PushConstants [[buffer(8)]]
#else
	Interpolators input,
#ifdef __spirv__
	in bool iFace : SV_IsFrontFace
#else
	in uint iFace : SV_IsFrontFace
#endif

#endif
)
{
#ifdef __air__
	PixelShaderOutput output = PixelShaderOutput{};
#else
	PixelShaderOutput output = (PixelShaderOutput)0;
#endif
#ifdef __spirv__
	output.oMask = 0xFFFFFFFFu;
#endif
#ifdef __air__
	float4 c252 = as_type<float4>(uint4(0x0, 0x3F800000, 0x43800000, 0x3F000000));
#else
	float4 c252 = asfloat(uint4(0x0, 0x3F800000, 0x43800000, 0x3F000000));
#endif
#ifdef __air__
	float4 c253 = as_type<float4>(uint4(0x3D93A92A, 0x3E59999A, 0x3F372474, 0xBF000000));
#else
	float4 c253 = asfloat(uint4(0x3D93A92A, 0x3E59999A, 0x3F372474, 0xBF000000));
#endif
#ifdef __air__
	float4 c254 = as_type<float4>(uint4(0x3E800000, 0x0, 0x0, 0x0));
#else
	float4 c254 = asfloat(uint4(0x3E800000, 0x0, 0x0, 0x0));
#endif
#ifdef __air__
	float4 c255 = as_type<float4>(uint4(0x40000000, 0x40800000, 0x41000000, 0x43800000));
#else
	float4 c255 = asfloat(uint4(0x40000000, 0x40800000, 0x41000000, 0x43800000));
#endif

	float4 r0 = input.iTexCoord0;
	float4 r1 = input.iTexCoord1;
	float4 r2 = 0.0;
	float4 r3 = 0.0;
	float4 r4 = 0.0;
	float4 r5 = 0.0;
	float4 r6 = 0.0;
	float4 r7 = 0.0;
	float4 r8 = 0.0;
	float4 r9 = 0.0;
	float4 r10 = 0.0;
	float4 r11 = 0.0;
	float4 r12 = 0.0;
	float4 r13 = 0.0;
	float4 r14 = 0.0;
	float4 r15 = 0.0;
	float4 r16 = 0.0;
	float4 r17 = 0.0;
	float4 r18 = 0.0;
	float4 r19 = 0.0;
	float4 r20 = 0.0;
	float4 r21 = 0.0;
	float4 r22 = 0.0;
	float4 r23 = 0.0;
	float4 r24 = 0.0;
	float4 r25 = 0.0;
	float4 r26 = 0.0;
	float4 r27 = 0.0;
	float4 r28 = 0.0;
	float4 r29 = 0.0;
	float4 r30 = 0.0;
	float4 r31 = 0.0;
	int a0 = 0;
	int aL = 0;
	bool p0 = false;
	float ps = 0.0;

	ps = -abs(r0.x) > 0.0;
	r0.z = ps;
	r8.xyz = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		BloomSampler_Texture2DDescriptorIndex, BloomSampler_SamplerDescriptorIndex, r0.xy, float2(0, 0)).xyz;
	r3.x = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		GBufferTextureSampler2_Texture2DDescriptorIndex, GBufferTextureSampler2_SamplerDescriptorIndex, r0.xy, float2(0, 0)).w;
	r0.z = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		AdapLumSampler_Texture2DDescriptorIndex, AdapLumSampler_SamplerDescriptorIndex, r0.zz, float2(0, 0)).x;
	r0.w = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		GBufferTextureSampler3_Texture2DDescriptorIndex, GBufferTextureSampler3_SamplerDescriptorIndex, r0.xy, float2(0, 0)).x;
	r4.xyz = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		HDRSampler_Texture2DDescriptorIndex, HDRSampler_SamplerDescriptorIndex, r0.xy, float2(-0.5, -1.5)).xyz;
	r3.yzw = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		HDRSampler_Texture2DDescriptorIndex, HDRSampler_SamplerDescriptorIndex, r0.xy, float2(0, 0)).xyz;
	r5.xyz = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		HDRSampler_Texture2DDescriptorIndex, HDRSampler_SamplerDescriptorIndex, r0.xy, float2(1.5, -0.5)).xyz;
	r6.xyz = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		HDRSampler_Texture2DDescriptorIndex, HDRSampler_SamplerDescriptorIndex, r0.xy, float2(0.5, 1.5)).xyz;
	r7.xyz = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		HDRSampler_Texture2DDescriptorIndex, HDRSampler_SamplerDescriptorIndex, r0.xy, float2(-1.5, 0.5)).xyz;
	r2.x = (float)((dot(r7.zxy, c253.xyz)));
	r1.x = (float)((dot(r6.zxy, c253.xyz)));
	r1.y = (float)((dot(r5.zxy, c253.xyz)));
	r1.z = (float)((dot(r3.wyz, c253.xyz)));
	r1.w = (float)((dot(r4.zxy, c253.xyz)));
	r2.zw = (float2)((r0.ww * dofProj.yx));
	ps = clamp(rcp(r0.z), -FLT_MAX, FLT_MAX);
	r0.x = ps;
	r3.xyzw = (float4)((r3.yzwx * c252.wwwz));
	r9.xyzw = (float4)((selectWrapper(c252.xyyy == 0.0, r2.xxxx, r1.xxwy)));
	r0.z = (float)((-r3.w >= c252.x));
	ps = ToneMapParams.y * r0.x;
	r0.w = ps;
	r10.xyzw = (float4)((c255.xyzw >= r3.wwww));
	ps = clamp(rcp(r0.w), -FLT_MAX, FLT_MAX);
	r0.y = ps;
	r8.w = (float)((r0.z + -r10.x));
	ps = dofProj.y * dofProj.x;
	r3.w = ps;
	r2.y = (float)((dot(r9.yzwx, c254.xxxx)));
	ps = ToneMapParams.x * r0.y;
	r0.x = ps;
	if (FusionModernEnabled()) {
	  r9.xyz = FusionBloomThreshold(r8.xyz * Exposure.x, r0.x);
	} else {
	r9.xyz = (float3)((r8.xyz * Exposure.xxx + -r0.xxx));
	}
	r1.xyzw = (float4)((-r2.yyyy + r1.zxyw));
	ps = r10.x - r10.y;
	r8.z = ps;
	r0.z = (float)((dot(r1.ywz, r1.ywz)));
	ps = r10.y - r10.z;
	r8.y = ps;
	r9.xyz = (float3)((max(r9.xyz, c252.xxx)));
	ps = r10.z - r10.w;
	r8.x = ps;
	r0.x = (float)((dot(r8.xyzw, c253.wwww)));
	ps = r2.z - r2.w;
	r0.y = ps;
	r8.xyzw = (float4)((r8.yxzw * c253.wwww));
	ps = dofProj.x + r0.y;
	r0.y = ps;
	r3.xyz = (float3)((r8.www * r4.xyz + r3.xyz));
	r0.x = (float)((r0.x + c252.w));
	ps = clamp(rcp(r0.y), -FLT_MAX, FLT_MAX);
	r0.y = ps;
	r1.yzw = (float3)((r9.xyz * ToneMapParams.zzz));
	ps = clamp(rcp(r0.x), -FLT_MAX, FLT_MAX);
	r0.x = ps;
	r3.xyz = (float3)((r8.zzz * r5.xyz + r3.xyz));
	r3.xyz = (float3)((r8.xxx * r6.xyz + r3.xyz));
	r3.xyz = (float3)((r8.yyy * r7.xyz + r3.xyz));
	r3.xyzw = (float4)((r3.xyzw * r0.xxxy));
	r8.x = (float)((-r3.w + dofDist.w));
	ps = dofBlur.x - dofBlur.y;
	r0.x = ps;
	r8.y = (float)((r3.w + -dofDist.w));
	ps = clamp(rcp(dofDist.x), -FLT_MAX, FLT_MAX);
	r2.z = ps;
	r8.xy = (float2)((-dofDist.yy * c252.ww + r8.xy));
	r8.xy = (float2)((max(r8.xy, c252.xx)));
	ps = clamp(rcp(dofDist.z), -FLT_MAX, FLT_MAX);
	r2.w = ps;
	r2.zw = (float2)((r8.xy * r2.zw));
	ps = dofBlur.z - dofBlur.y;
	r0.y = ps;
	r0.xy = (float2)((r2.zw * r0.xy + dofBlur.yy));
	r0.xy = (float2)((min(r0.xy, dofBlur.xz)));
	ps = max(-r2.y, -r2.y);
	r0.y = (float)((max(r0.x, r0.y)));
	ps = r2.x + ps;
	r0.x = ps;
	r0.xy = (float2)((r0.yx * r0.yx));
	r0.z = (float)((r0.z + r0.y));
	ps = r1.x * r1.x;
	r1.x = ps;
	r0.z = (float)((r1.x >= r0.z));
	ps = c252.y - r0.x;
	r1.x = ps;
	r1.x = (float)((r1.x * r0.z + r0.x));
	r2.xyzw = (float4)((r1.yzwx * c254.xxxx));
	r0.xyz = (float3)((r2.www * r7.xyz));
	ps = c252.y - r1.x;
	r1.x = ps;
	r0.xyz = (float3)((r2.www * r6.xyz + r0.xyz));
	r0.xyz = (float3)((r2.www * r5.xyz + r0.xyz));
	r0.xyz = (float3)((r2.www * r4.xyz + r0.xyz));
	r0.xyz = (float3)((r3.xyz * r1.xxx + r0.xyz));

	// Retain all title tone mapping, bloom, motion blur and grain after this point.
	if (LibertySplitPostFxApplied != 0.0f)
	{
		r0.xyz = tfetch2D(
#ifdef __air__
			g_Texture2DDescriptorHeap, g_SamplerDescriptorHeap,
#endif
			HDRSampler_Texture2DDescriptorIndex, HDRSampler_SamplerDescriptorIndex,
			input.iTexCoord0.xy, float2(0, 0), GetTextureLodBias(2)).xyz;
	}
	r0.xyz = (float3)((r0.xyz * Exposure.xxx + r2.xyz));
	r0.yzw = (float3)((r0.www * r0.xyz));
	r0.x = (float)((dot(r0.wyz, c253.xyz)));
	r2.xyz = (float3)((-r0.xxx + r0.yzw));
	ps = ColorShift.w * r0.x;
	r0.y = ps;
	r1.xyz = (float3)((r0.yyy * ColorShift.xyz));
	ps = max(deSatContrastGamma.z, deSatContrastGamma.z);
	r3.xyz = (float3)((r2.xyz * deSatContrastGamma.xxx + r0.xxx));
	r2.yz = (float2)((saturate(max(r0.yx, r0.yx))));
	ps = -c252.y + ps;
	r2.x = ps;
	r0.yzw = (float3)((r3.xyz + -r1.xyz));
	ps = clamp(log2(r2.z), -FLT_MAX, FLT_MAX);
	r0.x = ps;
	r0.xyzw = (float4)((r2.xyyy * r0.xyzw));
	r0.yzw = (float3)((r1.xyz + r0.yzw));
	ps = ColorCorrect.x + ColorCorrect.x;
	r1.x = ps;
	r0.y = (float)((r1.x * r0.y));
	r1.xy = (float2)((r0.zw * ColorCorrect.yz));
	r0.zw = (float2)((r1.xy + r1.xy));
	ps = exp2(r0.x);
	r0.x = ps;
	output.oC0.xyz = (float3)((r0.yzww * r0.xxxx).xyz);
	output.oC0.w = 1.0;
	BRANCH if (g_SpecConstants() & SPEC_CONSTANT_ALPHA_TEST)
	{
		uint alphaTestFunction = (g_SpecConstants() >> SPEC_CONSTANT_ALPHA_TEST_FUNCTION_SHIFT) & 7u;
		bool alphaTestPass =
			(alphaTestFunction == 0u && false) ||
			(alphaTestFunction == 1u && output.oC0.w < g_AlphaThreshold) ||
			(alphaTestFunction == 2u && output.oC0.w == g_AlphaThreshold) ||
			(alphaTestFunction == 3u && output.oC0.w <= g_AlphaThreshold) ||
			(alphaTestFunction == 4u && output.oC0.w > g_AlphaThreshold) ||
			(alphaTestFunction == 5u && (any(isnan(float2(output.oC0.w, g_AlphaThreshold))) || output.oC0.w != g_AlphaThreshold)) ||
			(alphaTestFunction == 6u && output.oC0.w >= g_AlphaThreshold) ||
			(alphaTestFunction == 7u && true);
		clip(alphaTestPass ? 1.0 : -1.0);
	}
	#ifdef __spirv__
	output.oMask = ComputeXenosAlphaToMask(output.oC0.w, input.iPos.xy, g_AlphaToMask, g_AlphaToMaskSampleCount);
	#endif
	output.oC0.rgb = FusionToneMap(output.oC0.rgb);
	return output;
}