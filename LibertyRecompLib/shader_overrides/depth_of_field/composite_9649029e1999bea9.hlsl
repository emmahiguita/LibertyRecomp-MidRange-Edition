// Xbox composite adapter. The original fused result is the fallback.
#include "../xenos_shader_common.h"
#include "split_composite.hlsli"

#ifdef __spirv__

#define AdapLumSampler_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 20)
#define AdapLumSampler_Texture2DArrayDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 124)
#define AdapLumSampler_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 228)
#define AdapLumSampler_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 332)
#define AdapLumSampler_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 436)
#define BloomSampler_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 16)
#define BloomSampler_Texture2DArrayDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 120)
#define BloomSampler_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 224)
#define BloomSampler_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 328)
#define BloomSampler_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 432)
#define BlurSampler_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 12)
#define BlurSampler_Texture2DArrayDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 116)
#define BlurSampler_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 220)
#define BlurSampler_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 324)
#define BlurSampler_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 428)
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
#define NoiseParams vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3456, 0x10)
#define ToneMapParams vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3392, 0x10)
#define deSatContrastGamma vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3408, 0x10)
#define dofBlur vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3376, 0x10)
#define dofDist vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3360, 0x10)
#define dofProj vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3344, 0x10)

#elif defined(__air__)

#define AdapLumSampler_Texture2DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 20)))
#define AdapLumSampler_Texture2DArrayDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 124)))
#define AdapLumSampler_Texture3DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 228)))
#define AdapLumSampler_TextureCubeDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 332)))
#define AdapLumSampler_SamplerDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 436)))
#define BloomSampler_Texture2DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 16)))
#define BloomSampler_Texture2DArrayDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 120)))
#define BloomSampler_Texture3DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 224)))
#define BloomSampler_TextureCubeDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 328)))
#define BloomSampler_SamplerDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 432)))
#define BlurSampler_Texture2DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 12)))
#define BlurSampler_Texture2DArrayDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 116)))
#define BlurSampler_Texture3DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 220)))
#define BlurSampler_TextureCubeDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 324)))
#define BlurSampler_SamplerDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 428)))
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
#define NoiseParams (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 3456)))
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
	float4 NoiseParams : packoffset(c216);
	float4 ToneMapParams : packoffset(c212);
	float4 deSatContrastGamma : packoffset(c213);
	float4 dofBlur : packoffset(c211);
	float4 dofDist : packoffset(c210);
	float4 dofProj : packoffset(c209);
};

cbuffer SharedConstants : register(b2, space4)
{
	uint AdapLumSampler_Texture2DDescriptorIndex : packoffset(c1.y);
	uint AdapLumSampler_Texture2DArrayDescriptorIndex : packoffset(c7.w);
	uint AdapLumSampler_Texture3DDescriptorIndex : packoffset(c14.y);
	uint AdapLumSampler_TextureCubeDescriptorIndex : packoffset(c20.w);
	uint AdapLumSampler_SamplerDescriptorIndex : packoffset(c27.y);
	uint BloomSampler_Texture2DDescriptorIndex : packoffset(c1.x);
	uint BloomSampler_Texture2DArrayDescriptorIndex : packoffset(c7.z);
	uint BloomSampler_Texture3DDescriptorIndex : packoffset(c14.x);
	uint BloomSampler_TextureCubeDescriptorIndex : packoffset(c20.z);
	uint BloomSampler_SamplerDescriptorIndex : packoffset(c27.x);
	uint BlurSampler_Texture2DDescriptorIndex : packoffset(c0.w);
	uint BlurSampler_Texture2DArrayDescriptorIndex : packoffset(c7.y);
	uint BlurSampler_Texture3DDescriptorIndex : packoffset(c13.w);
	uint BlurSampler_TextureCubeDescriptorIndex : packoffset(c20.y);
	uint BlurSampler_SamplerDescriptorIndex : packoffset(c26.w);
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
	float4 c248 = as_type<float4>(uint4(0x0, 0x0, 0x0, 0x0));
#else
	float4 c248 = asfloat(uint4(0x0, 0x0, 0x0, 0x0));
#endif
#ifdef __air__
	float4 c249 = as_type<float4>(uint4(0x0, 0x0, 0x0, 0x0));
#else
	float4 c249 = asfloat(uint4(0x0, 0x0, 0x0, 0x0));
#endif
#ifdef __air__
	float4 c250 = as_type<float4>(uint4(0x0, 0x0, 0x0, 0x0));
#else
	float4 c250 = asfloat(uint4(0x0, 0x0, 0x0, 0x0));
#endif
#ifdef __air__
	float4 c251 = as_type<float4>(uint4(0x3FE66666, 0x404CCCCD, 0xBF000000, 0x3F000000));
#else
	float4 c251 = asfloat(uint4(0x3FE66666, 0x404CCCCD, 0xBF000000, 0x3F000000));
#endif
#ifdef __air__
	float4 c252 = as_type<float4>(uint4(0x0, 0x3F800000, 0x43800000, 0x3F000000));
#else
	float4 c252 = asfloat(uint4(0x0, 0x3F800000, 0x43800000, 0x3F000000));
#endif
#ifdef __air__
	float4 c253 = as_type<float4>(uint4(0x3D93A92A, 0x3E59999A, 0x3F372474, 0x3F8CCCCD));
#else
	float4 c253 = asfloat(uint4(0x3D93A92A, 0x3E59999A, 0x3F372474, 0x3F8CCCCD));
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

	r0.zw = (float2)((r0.xy * c251.yx + NoiseParams.xy));
	r1.xy = (float2)((frac(r0.zw)));
	ps = -abs(r0.x) > 0.0;
	r0.z = ps;
	r3.y = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		BlurSampler_Texture2DDescriptorIndex, BlurSampler_SamplerDescriptorIndex, r1.xy, float2(0, 0)).z;
	r1.yzw = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		BloomSampler_Texture2DDescriptorIndex, BloomSampler_SamplerDescriptorIndex, r0.xy, float2(0, 0)).xyz;
	r6.x = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		GBufferTextureSampler2_Texture2DDescriptorIndex, GBufferTextureSampler2_SamplerDescriptorIndex, r0.xy, float2(0, 0)).w;
	r2.x = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		AdapLumSampler_Texture2DDescriptorIndex, AdapLumSampler_SamplerDescriptorIndex, r0.zz, float2(0, 0)).x;
	r0.z = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		GBufferTextureSampler3_Texture2DDescriptorIndex, GBufferTextureSampler3_SamplerDescriptorIndex, r0.xy, float2(0, 0)).x;
	r2.yzw = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		HDRSampler_Texture2DDescriptorIndex, HDRSampler_SamplerDescriptorIndex, r0.xy, float2(-0.5, -1.5)).xyz;
	r6.yzw = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		HDRSampler_Texture2DDescriptorIndex, HDRSampler_SamplerDescriptorIndex, r0.xy, float2(0, 0)).xyz;
	r4.xyz = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		HDRSampler_Texture2DDescriptorIndex, HDRSampler_SamplerDescriptorIndex, r0.xy, float2(1.5, -0.5)).xyz;
	r5.xyz = tfetch2D(
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
	r8.x = (float)((NoiseParams.z * c253.w));
	r1.x = (float)((dofDist.y * c252.w));
	r3.x = (float)((dot(r7.zxy, c253.xyz)));
	r9.x = (float)((dot(r5.zxy, c253.xyz)));
	r9.y = (float)((dot(r4.zxy, c253.xyz)));
	r9.z = (float)((dot(r6.wyz, c253.xyz)));
	r9.w = (float)((dot(r2.wyz, c253.xyz)));
	r0.zw = (float2)((r0.zz * dofProj.yx));
	ps = clamp(rcp(r2.x), -FLT_MAX, FLT_MAX);
	r0.x = ps;
	r12.xyzw = (float4)((r6.xyzw * c252.zwww));
	ps = ToneMapParams.y * r0.x;
	r2.x = ps;
	r6.xyzw = (float4)((selectWrapper(c252.xyyy == 0.0, r3.xxxx, r9.xxwy)));
	r11.xyzw = (float4)((c255.yzwx >= r12.xxxx));
	ps = clamp(rcp(r2.x), -FLT_MAX, FLT_MAX);
	r0.y = ps;
	r0.x = (float)((dot(r6.yzwx, c254.xxxx)));
	r6.z = (float)((-r0.x + r3.x));
	ps = dofProj.y * dofProj.x;
	r3.x = ps;
	r10.yzw = (float3)((r1.yzw * Exposure.xxx));
	ps = -r12.x >= 0.0;
	r1.y = ps;
	r11.xyz = (float3)((r11.yxw + -r11.zyx));
	ps = max(r1.y, r1.y);
	r9.xyzw = (float4)((-r0.xxxx + r9.xyzw));
	ps = -r11.w + ps;
	r11.w = ps;
	r3.z = (float)((dot(r11.xyzw, c251.zzzz)));
	ps = r0.z - r0.w;
	r0.z = ps;
	r11.xyzw = (float4)((r11.yxzw * c251.zzzz));
	ps = dofProj.x + r0.z;
	r0.x = ps;
	r1.yzw = (float3)((r11.www * r2.yzw + r12.yzw));
	r1.yzw = (float3)((r11.zzz * r4.xyz + r1.yzw));
	r6.xy = (float2)((r3.yz + c251.zw));
	ps = clamp(rcp(r0.x), -FLT_MAX, FLT_MAX);
	r0.x = ps;
	r0.w = (float)((dot(r9.xwy, r9.xwy)));
	ps = clamp(rcp(r6.y), -FLT_MAX, FLT_MAX);
	r0.z = ps;
	r1.yzw = (float3)((r11.xxx * r5.xyz + r1.yzw));
	r3.yzw = (float3)((r11.yyy * r7.xyz + r1.yzw));
	r3.xyzw = (float4)((r3.xyzw * r0.xzzz));
	r10.x = (float)((r3.x + -dofDist.w));
	ps = ToneMapParams.x * r0.y;
	r1.y = ps;
	const float3 fusion_bloom = FusionBloomThreshold(r10.yzw, r1.y);
	r1.xyzw = (float4)((r10.xyzw + -r1.xyyy));
	if (FusionModernEnabled()) r1.yzw = fusion_bloom;
	r1.xyzw = (float4)((max(r1.zwyx, c252.xxxx)));
	ps = clamp(rcp(dofDist.z), -FLT_MAX, FLT_MAX);
	r0.x = ps;
	r0.y = (float)((r1.w * r0.x));
	ps = dofBlur.z - dofBlur.y;
	r0.x = ps;
	r0.x = (float)((r0.y * r0.x + dofBlur.y));
	r6.w = (float)((min(r0.x, dofBlur.z)));
	ps = r9.z * r9.z;
	r4.w = ps;
	r0.yz = (float2)((r6.zw * r6.zw));
	ps = max(r1.z, r1.z);
	r0.x = ps;
	r0.w = (float)((r0.w + r0.y));
	ps = ToneMapParams.z * r0.x;
	r0.x = ps;
	r6.z = (float)((r4.w >= r0.w));
	ps = c252.y - r0.z;
	r8.y = ps;
	r6.xy = (float2)((r8.xy * r6.xz));
	ps = ToneMapParams.z * r1.x;
	r0.y = ps;
	r0.w = (float)((r0.z + r6.y));
	ps = ToneMapParams.z * r1.y;
	r0.z = ps;
	r1.xyzw = (float4)((r0.xyzw * c254.xxxx));
	ps = c252.y - r0.w;
	r0.w = ps;
	r0.xyz = (float3)((r1.www * r7.xyz + r6.xxx));
	r0.xyz = (float3)((r1.www * r5.xyz + r0.xyz));
	r0.xyz = (float3)((r1.www * r4.xyz + r0.xyz));
	r0.xyz = (float3)((r1.www * r2.yzw + r0.xyz));
	r0.xyz = (float3)((r3.yzw * r0.www + r0.xyz));

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
	r0.xyz = (float3)((r0.xyz * Exposure.xxx + r1.xyz));
	r0.yzw = (float3)((r2.xxx * r0.xyz));
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