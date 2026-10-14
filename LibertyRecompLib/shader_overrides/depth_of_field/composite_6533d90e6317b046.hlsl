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
#define ColorCorrect vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3504, 0x10)
#define ColorShift vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3520, 0x10)
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
#define JitterSampler_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 20)
#define JitterSampler_Texture2DArrayDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 124)
#define JitterSampler_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 228)
#define JitterSampler_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 332)
#define JitterSampler_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 436)
#define PLAYER_MASK vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3536, 0x10)
#define StencilCopySampler_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 24)
#define StencilCopySampler_Texture2DArrayDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 128)
#define StencilCopySampler_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 232)
#define StencilCopySampler_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 336)
#define StencilCopySampler_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 440)
#define ToneMapParams vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3472, 0x10)
#define deSatContrastGamma vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3488, 0x10)
#define dofBlur vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3376, 0x10)
#define dofDist vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3360, 0x10)
#define dofProj vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3344, 0x10)
#define gDirectionalMotionBlurLength vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3392, 0x10)
#define globalScreenSize vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 704, 0x10)
#define motionBlurMatrix(INDEX) selectWrapper((INDEX) < 11, vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + (213 + min(INDEX, 10)) * 16, 0x10), 0.0)

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
#define ColorCorrect (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 3504)))
#define ColorShift (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 3520)))
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
#define JitterSampler_Texture2DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 20)))
#define JitterSampler_Texture2DArrayDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 124)))
#define JitterSampler_Texture3DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 228)))
#define JitterSampler_TextureCubeDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 332)))
#define JitterSampler_SamplerDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 436)))
#define PLAYER_MASK (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 3536)))
#define StencilCopySampler_Texture2DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 24)))
#define StencilCopySampler_Texture2DArrayDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 128)))
#define StencilCopySampler_Texture3DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 232)))
#define StencilCopySampler_TextureCubeDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 336)))
#define StencilCopySampler_SamplerDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 440)))
#define ToneMapParams (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 3472)))
#define deSatContrastGamma (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 3488)))
#define dofBlur (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 3376)))
#define dofDist (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 3360)))
#define dofProj (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 3344)))
#define gDirectionalMotionBlurLength (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 3392)))
#define globalScreenSize (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 704)))
#define motionBlurMatrix(INDEX) selectWrapper((INDEX) < 11, (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + (213 + min((uint)(INDEX), (uint)10)) * 16))), 0.0)

#else

cbuffer PixelShaderConstants : register(b1, space4)
{
	float4 ColorCorrect : packoffset(c219);
	float4 ColorShift : packoffset(c220);
	float4 Exposure : packoffset(c208);
	float4 PLAYER_MASK : packoffset(c221);
	float4 ToneMapParams : packoffset(c217);
	float4 deSatContrastGamma : packoffset(c218);
	float4 dofBlur : packoffset(c211);
	float4 dofDist : packoffset(c210);
	float4 dofProj : packoffset(c209);
	float4 gDirectionalMotionBlurLength : packoffset(c212);
	float4 globalScreenSize : packoffset(c44);
	float4 motionBlurMatrix[4] : packoffset(c213);
#define motionBlurMatrix(INDEX) selectWrapper((INDEX) < 11, motionBlurMatrix[min(INDEX, 10)], 0.0)
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
	uint JitterSampler_Texture2DDescriptorIndex : packoffset(c1.y);
	uint JitterSampler_Texture2DArrayDescriptorIndex : packoffset(c7.w);
	uint JitterSampler_Texture3DDescriptorIndex : packoffset(c14.y);
	uint JitterSampler_TextureCubeDescriptorIndex : packoffset(c20.w);
	uint JitterSampler_SamplerDescriptorIndex : packoffset(c27.y);
	uint StencilCopySampler_Texture2DDescriptorIndex : packoffset(c1.z);
	uint StencilCopySampler_Texture2DArrayDescriptorIndex : packoffset(c8.x);
	uint StencilCopySampler_Texture3DDescriptorIndex : packoffset(c14.z);
	uint StencilCopySampler_TextureCubeDescriptorIndex : packoffset(c21.x);
	uint StencilCopySampler_SamplerDescriptorIndex : packoffset(c27.z);
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
	float4 c250 = as_type<float4>(uint4(0x3E800000, 0x3E000000, 0x0, 0x0));
#else
	float4 c250 = asfloat(uint4(0x3E800000, 0x3E000000, 0x0, 0x0));
#endif
#ifdef __air__
	float4 c251 = as_type<float4>(uint4(0x43800000, 0x3F000000, 0x3F600000, 0xBF800000));
#else
	float4 c251 = asfloat(uint4(0x43800000, 0x3F000000, 0x3F600000, 0xBF800000));
#endif
#ifdef __air__
	float4 c252 = as_type<float4>(uint4(0x0, 0x3F800000, 0x3EC00000, 0x3F000000));
#else
	float4 c252 = asfloat(uint4(0x0, 0x3F800000, 0x3EC00000, 0x3F000000));
#endif
#ifdef __air__
	float4 c253 = as_type<float4>(uint4(0x3F200000, 0x3F400000, 0x423C851F, 0x4268A7F0));
#else
	float4 c253 = asfloat(uint4(0x3F200000, 0x3F400000, 0x423C851F, 0x4268A7F0));
#endif
#ifdef __air__
	float4 c254 = as_type<float4>(uint4(0x3D93A92A, 0x3E59999A, 0x3F372474, 0xBF000000));
#else
	float4 c254 = asfloat(uint4(0x3D93A92A, 0x3E59999A, 0x3F372474, 0xBF000000));
#endif
#ifdef __air__
	float4 c255 = as_type<float4>(uint4(0x41000000, 0x40800000, 0x40000000, 0x43800000));
#else
	float4 c255 = asfloat(uint4(0x41000000, 0x40800000, 0x40000000, 0x43800000));
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

	r0.w = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		StencilCopySampler_Texture2DDescriptorIndex, StencilCopySampler_SamplerDescriptorIndex, r0.xy, float2(0, 0)).z;
	r10.z = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		GBufferTextureSampler3_Texture2DDescriptorIndex, GBufferTextureSampler3_SamplerDescriptorIndex, r0.xy, float2(0, 0)).x;
	r2.x = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		GBufferTextureSampler2_Texture2DDescriptorIndex, GBufferTextureSampler2_SamplerDescriptorIndex, r0.xy, float2(0, 0)).w;
	r6.xyz = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		HDRSampler_Texture2DDescriptorIndex, HDRSampler_SamplerDescriptorIndex, r0.xy, float2(-0.5, -1.5)).xyz;
	r2.yzw = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		HDRSampler_Texture2DDescriptorIndex, HDRSampler_SamplerDescriptorIndex, r0.xy, float2(0, 0)).xyz;
	r7.xyz = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		HDRSampler_Texture2DDescriptorIndex, HDRSampler_SamplerDescriptorIndex, r0.xy, float2(1.5, -0.5)).xyz;
	r8.xyz = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		HDRSampler_Texture2DDescriptorIndex, HDRSampler_SamplerDescriptorIndex, r0.xy, float2(0.5, 1.5)).xyz;
	r3.xyz = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		HDRSampler_Texture2DDescriptorIndex, HDRSampler_SamplerDescriptorIndex, r0.xy, float2(-1.5, 0.5)).xyz;
	r1.yzw = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		BloomSampler_Texture2DDescriptorIndex, BloomSampler_SamplerDescriptorIndex, r0.xy, float2(0, 0)).xyz;
	r10.xy = (float2)((r0.yx * c255.zz + c251.ww));
	r11.w = (float)((-r10.x * dofProj.w));
	r9.z = (float)((dot(r3.zxy, c254.xyz)));
	r4.x = (float)((dot(r8.zxy, c254.xyz)));
	r4.y = (float)((dot(r7.zxy, c254.xyz)));
	r4.z = (float)((dot(r2.wyz, c254.xyz)));
	r4.w = (float)((dot(r6.zxy, c254.xyz)));
	r5.xyzw = (float4)((r2.xyzw * c251.xyyy));
	ps = -abs(r0.x) > 0.0;
	r2.w = ps;
	r14.xyzw = (float4)((selectWrapper(c252.xyyy == 0.0, r9.zzzz, r4.xxwy)));
	r13.xyzw = (float4)((c255.xyzw >= r5.xxxx));
	ps = -r5.x >= 0.0;
	r0.z = ps;
	r12.w = (float)((r0.z + -r13.z));
	ps = dofProj.y * dofProj.x;
	r5.x = ps;
	r10.w = (float)((dot(r14.yzwx, c250.xxxx)));
	ps = r13.x - r13.w;
	r12.x = ps;
	r4.xyzw = (float4)((-r10.wwww + r4.xyzw));
	ps = r13.z - r13.y;
	r12.y = ps;
	r11.xyz = (float3)((r10.yzz * dofProj.zyx));
	ps = r13.y - r13.x;
	r12.z = ps;
	r0.z = (float)((dot(r12.xzyw, c254.wwww)));
	ps = r11.y - r11.z;
	r2.x = ps;
	r12.xyzw = (float4)((r12.wyxz * c254.wwww));
	ps = c252.w + r0.z;
	r1.x = ps;
	r5.yzw = (float3)((r12.xxx * r6.zyx + r5.wzy));
	r5.yzw = (float3)((r12.yyy * r7.zyx + r5.yzw));
	r0.z = (float)((r2.x + dofProj.x));
	ps = clamp(rcp(r1.x), -FLT_MAX, FLT_MAX);
	r2.y = ps;
	r4.z = (float)((r4.z * r4.z));
	ps = clamp(rcp(r0.z), -FLT_MAX, FLT_MAX);
	r2.x = ps;
	r5.yzw = (float3)((r12.www * r8.zyx + r5.yzw));
	r5.yzw = (float3)((r12.zzz * r3.zyx + r5.yzw));
	r5.xyzw = (float4)((r5.xyzw * r2.xyyy));
	ps = dofBlur.z - dofBlur.y;
	r9.w = ps;
	r3.w = (float)((r0.w >= PLAYER_MASK.x));
	ps = max(r5.x, r5.x);
	r0.z = ps;
	r2.xz = (float2)((r11.xw * r5.xx));
	ps = -dofDist.w - -r0.z;
	r0.z = ps;
	r11.xyz = (float3)((-r5.xxx * motionBlurMatrix(2).zxy + motionBlurMatrix(3).zxy));
	r11.xyz = (float3)((r2.zzz * motionBlurMatrix(1).zxy + r11.xyz));
	r0.z = (float)((-dofDist.y * c252.w + r0.z));
	r1.x = (float)((max(r0.z, c252.x)));
	ps = Exposure.x * r1.w;
	r2.z = ps;
	r11.xyz = (float3)((r2.xxx * motionBlurMatrix(0).zxy + r11.xyz));
	r2.x = (float)((-r11.x * dofProj.z));
	ps = clamp(rcp(dofDist.z), -FLT_MAX, FLT_MAX);
	r0.w = ps;
	r0.z = (float)((r11.x * dofProj.w));
	ps = clamp(rcp(r2.x), -FLT_MAX, FLT_MAX);
	r9.x = ps;
	r11.w = (float)((r1.x * r0.w));
	ps = clamp(rcp(r0.z), -FLT_MAX, FLT_MAX);
	r9.y = ps;
	r9.xyw = (float3)((r11.yzw * r9.xyw));
	ps = Exposure.x * r1.z;
	r2.y = ps;
	r0.z = (float)((r9.w + dofBlur.y));
	ps = Exposure.x * r1.y;
	r2.x = ps;
	r1.y = (float)((dot(r4.xwy, r4.xwy)));
	ps = max(r0.z, r0.z);
	r0.z = ps;
	r9.yzw = (float3)((-r10.yxw + r9.xyz));
	ps = max(dofBlur.z, dofBlur.z);
	r0.w = ps;
	r1.xw = (float2)((r9.yz * gDirectionalMotionBlurLength.xx));
	ps = min(r0.z, r0.w);
	r9.x = ps;
	r0.zw = (float2)((r9.xw * r9.xw));
	ps = c250.y * r1.x;
	r4.x = ps;
	r1.y = (float)((r1.y + r0.w));
	ps = c250.y * r1.w;
	r4.y = ps;
	r1.y = (float)((r4.z >= r1.y));
	ps = c252.y - r0.z;
	r1.z = ps;
	r0.z = (float)((r1.z * r1.y + r0.z));
	r1.y = (float)((r0.z * c250.x));
	ps = c252.y - r0.z;
	r0.w = ps;
	r3.xyz = (float3)((r1.yyy * r3.zyx));
	p0 = r3.w == 0.0;
	ps = p0 ? 0.0 : 1.0;
	r3.xyz = (float3)((r1.yyy * r8.zyx + r3.xyz));
	r3.xyz = (float3)((r1.yyy * r7.zyx + r3.xyz));
	r3.xyz = (float3)((r1.yyy * r6.zyx + r3.xyz));
	r3.xyz = (float3)((r5.wzy * r0.www + r3.zyx));

	// Retain all title tone mapping, bloom, motion blur and grain after this point.
	if (LibertySplitPostFxApplied != 0.0f)
	{
		r3.xyz = tfetch2D(
#ifdef __air__
			g_Texture2DDescriptorHeap, g_SamplerDescriptorHeap,
#endif
			HDRSampler_Texture2DDescriptorIndex, HDRSampler_SamplerDescriptorIndex,
			input.iTexCoord0.xy, float2(0, 0), GetTextureLodBias(2)).xyz;
	}
	if (p0)
	{
		r1.yz = (float2)((r3.xy * c255.xx));
	}
	if (p0)
	{
		r1.yz = (float2)((r0.xy * c253.wz + r1.yz));
	}
	if (p0)
	{
		r1.y = tfetch2D(
#ifdef __air__
			g_Texture2DDescriptorHeap,
			g_SamplerDescriptorHeap,
#endif
			JitterSampler_Texture2DDescriptorIndex, JitterSampler_SamplerDescriptorIndex, r1.yz, float2(0, 0)).x;
	}
	if (p0)
	{
		ps = -c252.w - -r1.y;
		r0.w = ps;
	}
	if (p0)
	{
		r5.zw = (float2)((r4.xy + r4.xy));
		ps = c251.z * r1.x;
		r5.x = ps;
	}
	if (p0)
	{
		r1.yz = (float2)((r4.xy * r0.ww + r0.xy));
	}
	if (p0)
	{
		r0.xy = (float2)((r1.yz + r4.xy));
		ps = c251.z * r1.w;
		r5.y = ps;
	}
	if (p0)
	{
		r13.xyzw = (float4)((r1.yzyz + r5.xyzw));
	}
	if (p0)
	{
		r9.xyzw = (float4)((r1.xxww * c252.wzzw + r1.yyzz));
	}
	if (p0)
	{
		r14.xyzw = (float4)((r1.xwxw * c253.xxyy + r1.yzyz));
	}
	if (p0)
	{
		r5.xyz = tfetch2D(
#ifdef __air__
			g_Texture2DDescriptorHeap,
			g_SamplerDescriptorHeap,
#endif
			HDRSampler_Texture2DDescriptorIndex, HDRSampler_SamplerDescriptorIndex, r14.zw, float2(0, 0)).xyz;
	}
	if (p0)
	{
		r7.xyz = tfetch2D(
#ifdef __air__
			g_Texture2DDescriptorHeap,
			g_SamplerDescriptorHeap,
#endif
			HDRSampler_Texture2DDescriptorIndex, HDRSampler_SamplerDescriptorIndex, r14.xy, float2(0, 0)).xyz;
	}
	if (p0)
	{
		r8.xyz = tfetch2D(
#ifdef __air__
			g_Texture2DDescriptorHeap,
			g_SamplerDescriptorHeap,
#endif
			HDRSampler_Texture2DDescriptorIndex, HDRSampler_SamplerDescriptorIndex, r9.xw, float2(0, 0)).xyz;
	}
	if (p0)
	{
		r10.xyz = tfetch2D(
#ifdef __air__
			g_Texture2DDescriptorHeap,
			g_SamplerDescriptorHeap,
#endif
			HDRSampler_Texture2DDescriptorIndex, HDRSampler_SamplerDescriptorIndex, r9.yz, float2(0, 0)).xyz;
	}
	if (p0)
	{
		r11.xyz = tfetch2D(
#ifdef __air__
			g_Texture2DDescriptorHeap,
			g_SamplerDescriptorHeap,
#endif
			HDRSampler_Texture2DDescriptorIndex, HDRSampler_SamplerDescriptorIndex, r13.zw, float2(0, 0)).xyz;
	}
	if (p0)
	{
		r4.yzw = tfetch2D(
#ifdef __air__
			g_Texture2DDescriptorHeap,
			g_SamplerDescriptorHeap,
#endif
			HDRSampler_Texture2DDescriptorIndex, HDRSampler_SamplerDescriptorIndex, r0.xy, float2(0, 0)).xyz;
	}
	if (p0)
	{
		r12.xyz = tfetch2D(
#ifdef __air__
			g_Texture2DDescriptorHeap,
			g_SamplerDescriptorHeap,
#endif
			HDRSampler_Texture2DDescriptorIndex, HDRSampler_SamplerDescriptorIndex, r13.xy, float2(0, 0)).xyz;
	}
	if (p0)
	{
		r6.x = tfetch2D(
#ifdef __air__
			g_Texture2DDescriptorHeap,
			g_SamplerDescriptorHeap,
#endif
			StencilCopySampler_Texture2DDescriptorIndex, StencilCopySampler_SamplerDescriptorIndex, r13.xy, float2(0, 0)).z;
	}
	if (p0)
	{
		r6.y = tfetch2D(
#ifdef __air__
			g_Texture2DDescriptorHeap,
			g_SamplerDescriptorHeap,
#endif
			StencilCopySampler_Texture2DDescriptorIndex, StencilCopySampler_SamplerDescriptorIndex, r14.xy, float2(0, 0)).z;
	}
	if (p0)
	{
		r6.z = tfetch2D(
#ifdef __air__
			g_Texture2DDescriptorHeap,
			g_SamplerDescriptorHeap,
#endif
			StencilCopySampler_Texture2DDescriptorIndex, StencilCopySampler_SamplerDescriptorIndex, r14.zw, float2(0, 0)).z;
	}
	if (p0)
	{
		r9.x = tfetch2D(
#ifdef __air__
			g_Texture2DDescriptorHeap,
			g_SamplerDescriptorHeap,
#endif
			StencilCopySampler_Texture2DDescriptorIndex, StencilCopySampler_SamplerDescriptorIndex, r9.xw, float2(0, 0)).z;
	}
	if (p0)
	{
		r9.y = tfetch2D(
#ifdef __air__
			g_Texture2DDescriptorHeap,
			g_SamplerDescriptorHeap,
#endif
			StencilCopySampler_Texture2DDescriptorIndex, StencilCopySampler_SamplerDescriptorIndex, r9.yz, float2(0, 0)).z;
	}
	if (p0)
	{
		r9.z = tfetch2D(
#ifdef __air__
			g_Texture2DDescriptorHeap,
			g_SamplerDescriptorHeap,
#endif
			StencilCopySampler_Texture2DDescriptorIndex, StencilCopySampler_SamplerDescriptorIndex, r0.xy, float2(0, 0)).z;
	}
	if (p0)
	{
		r9.w = tfetch2D(
#ifdef __air__
			g_Texture2DDescriptorHeap,
			g_SamplerDescriptorHeap,
#endif
			StencilCopySampler_Texture2DDescriptorIndex, StencilCopySampler_SamplerDescriptorIndex, r13.zw, float2(0, 0)).z;
	}
	if (p0)
	{
		r0.xy = (float2)((r1.xw * globalScreenSize.xy));
	}
	if (p0)
	{
		r0.x = (float)((dot(r0.xy, r0.xy) + c252.x));
	}
	if (p0)
	{
		r9.xyzw = (float4)((r9.xyzw >= PLAYER_MASK.xxxx));
	}
	if (p0)
	{
		r1.yzw = (float3)((r6.xyz >= PLAYER_MASK.xxx));
	}
	if (p0)
	{
		r4.x = (float)((dot(-r1.zyw, c252.yyy)));
	}
	if (p0)
	{
		r1.x = (float)((dot(-r9.zwyx, c252.yyyy)));
	}
	if (p0)
	{
		r6.xyz = (float3)((-r1.zyw + c252.yyy));
	}
	if (p0)
	{
		r9.xyzw = (float4)((-r9.ywxz + c252.yyyy));
		ps = sqrt(abs(r0.x));
		r0.x = ps;
	}
	if (p0)
	{
		r1.yzw = (float3)((r6.yyy * r12.zyx));
		ps = c252.y + r0.z;
		r0.y = ps;
	}
	if (p0)
	{
		r4.yzw = (float3)((r9.www * r4.yzw + r3.xyz));
	}
	if (p0)
	{
		r4.yzw = (float3)((r9.yyy * r11.xyz + r4.yzw));
	}
	if (p0)
	{
		r4.yzw = (float3)((r9.xxx * r10.xyz + r4.yzw));
	}
	if (p0)
	{
		r4.yzw = (float3)((r9.zzz * r8.xyz + r4.yzw));
	}
	if (p0)
	{
		r4.yzw = (float3)((r6.xxx * r7.xyz + r4.yzw));
	}
	if (p0)
	{
		r4.yzw = (float3)((r6.zzz * r5.xyz + r4.yzw));
	}
	if (p0)
	{
		r1.xyzw = (float4)((r4.xyzw + r1.xwzy));
		ps = c252.w * r0.x;
		r0.z = ps;
	}
	if (p0)
	{
		r0.x = (float)((r1.x + c255.x));
		ps = clamp(rcp(r0.y), -FLT_MAX, FLT_MAX);
		r0.y = ps;
	}
	if (p0)
	{
		r0.w = (float)((saturate(r0.z * r0.y)));
		ps = clamp(rcp(r0.x), -FLT_MAX, FLT_MAX);
		r0.x = ps;
	}
	if (p0)
	{
		r0.xyz = (float3)((r1.yzw * r0.xxx + -r3.xyz));
	}
	if (p0)
	{
		r3.xyz = (float3)((r0.www * r0.xyz + r3.xyz));
	}
	r0.x = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		AdapLumSampler_Texture2DDescriptorIndex, AdapLumSampler_SamplerDescriptorIndex, r2.ww, float2(0, 0)).x;
	ps = clamp(rcp(r0.x), -FLT_MAX, FLT_MAX);
	r0.x = ps;
	ps = ToneMapParams.y * r0.x;
	r0.w = ps;
	r4.x = (float)((deSatContrastGamma.z + -c252.y));
	ps = clamp(rcp(r0.w), -FLT_MAX, FLT_MAX);
	r0.x = ps;
	if (FusionModernEnabled()) {
	  r0.xyz = FusionBloomThreshold(r2.xyz, r0.x * ToneMapParams.x);
	} else {
	r0.xyz = (float3)((-r0.xxx * ToneMapParams.xxx + r2.xyz));
	}
	r0.xyz = (float3)((max(r0.xyz, c252.xxx)));
	r0.xyz = (float3)((r0.xyz * ToneMapParams.zzz));
	r0.xyz = (float3)((r0.xyz * c250.xxx));
	r0.xyz = (float3)((r3.xyz * Exposure.xxx + r0.xyz));
	r0.yzw = (float3)((r0.www * r0.xyz));
	r0.x = (float)((dot(r0.wyz, c254.xyz)));
	r2.xyz = (float3)((-r0.xxx + r0.yzw));
	ps = ColorShift.w * r0.x;
	r0.y = ps;
	r1.xyz = (float3)((r0.yyy * ColorShift.xyz));
	r2.xyz = (float3)((r2.xyz * deSatContrastGamma.xxx + r0.xxx));
	r4.yz = (float2)((saturate(max(r0.yx, r0.yx))));
	r0.yzw = (float3)((r2.xyz + -r1.xyz));
	ps = clamp(log2(r4.z), -FLT_MAX, FLT_MAX);
	r0.x = ps;
	r0.xyzw = (float4)((r4.xyyy * r0.xyzw));
	r1.xyz = (float3)((r1.xyz + r0.yzw));
	r1.xyz = (float3)((r1.xyz * ColorCorrect.xyz));
	r0.yzw = (float3)((r1.xyz + r1.xyz));
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