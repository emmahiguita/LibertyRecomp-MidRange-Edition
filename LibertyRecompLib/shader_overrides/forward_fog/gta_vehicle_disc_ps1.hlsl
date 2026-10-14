// GTA IV native shader override ABI.
// Tracked here so cache generation is independent of the local XenosRecomp fork.
#ifndef SHADER_COMMON_H_INCLUDED
#define SHADER_COMMON_H_INCLUDED

#define SPEC_CONSTANT_R11G11B10_NORMAL  (1 << 0)
#define SPEC_CONSTANT_ALPHA_TEST        (1 << 1)
#define SPEC_CONSTANT_ALPHA_TEST_FUNCTION_SHIFT 8
#define SPEC_CONSTANT_ALPHA_TEST_FUNCTION_MASK  0x700
#define SPEC_CONSTANT_ALPHA_TEST_CAPABILITY_MASK 0x702

#ifdef UNLEASHED_RECOMP
    #define SPEC_CONSTANT_BICUBIC_GI_FILTER (1 << 2)
    #define SPEC_CONSTANT_ALPHA_TO_COVERAGE (1 << 3)
    #define SPEC_CONSTANT_REVERSE_Z         (1 << 4)
#endif

#ifdef MARATHON_RECOMP
    #define SPEC_CONSTANT_CONDITIONAL_RENDERING (1 << 5)
#endif

#if defined(__air__) || !defined(__cplusplus) || defined(__INTELLISENSE__)

#ifndef __air__
#define FLT_MIN asfloat(0xff7fffff)
#define FLT_MAX asfloat(0x7f7fffff)
#endif

#ifdef __spirv__

struct PushConstants
{
    uint64_t VertexShaderConstants;
    uint64_t PixelShaderConstants;
    uint64_t SharedConstants;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> g_PushConstants;

#ifdef GTA4_RECOMP
#define GetTextureLodBias(SLOT) vk::RawBufferLoad<float>(g_PushConstants.SharedConstants + 752 + (SLOT) * 4)
#define g_Booleans                  vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 528)
#define g_SwappedTexcoords          vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 532)
#define g_SwappedNormals            vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 536)
#define g_SwappedBinormals          vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 540)
#define g_SwappedTangents           vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 544)
#define g_SwappedBlendWeights       vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 548)
#define g_HalfPixelOffset           vk::RawBufferLoad<float2>(g_PushConstants.SharedConstants + 552)
#define g_ClipPlane                 vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + 560)
#define g_ClipPlaneEnabled          vk::RawBufferLoad<bool>(g_PushConstants.SharedConstants + 576)
#define g_AlphaThreshold            vk::RawBufferLoad<float>(g_PushConstants.SharedConstants + 580)
#define g_conditionalSurveyIndex    vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 584)
#define g_conditionalRenderingIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 588)
#define g_AlphaToMaskSampleCount    vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 740)
#define g_AlphaToMask               vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 736)
#else
#define g_Booleans                  vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 320)
#define g_SwappedTexcoords          vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 324)
#define g_SwappedNormals            vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 328)
#define g_SwappedBinormals          vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 332)
#define g_SwappedTangents           vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 336)
#define g_SwappedBlendWeights       vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 340)
#define g_HalfPixelOffset           vk::RawBufferLoad<float2>(g_PushConstants.SharedConstants + 344)
#define g_ClipPlane                 vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + 352)
#define g_ClipPlaneEnabled          vk::RawBufferLoad<bool>(g_PushConstants.SharedConstants + 368)
#define g_AlphaThreshold            vk::RawBufferLoad<float>(g_PushConstants.SharedConstants + 372)
#define g_conditionalSurveyIndex    vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 376)
#define g_conditionalRenderingIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 380)
#endif

[[vk::constant_id(0)]] const uint g_SpecConstants = 0;

#define g_SpecConstants() g_SpecConstants

#elif defined(__air__)

#include <metal_stdlib>

using namespace metal;

constant uint G_SPEC_CONSTANTS [[function_constant(0)]];
constant uint G_SPEC_CONSTANTS_VAL = is_function_constant_defined(G_SPEC_CONSTANTS) ? G_SPEC_CONSTANTS : 0;

uint g_SpecConstants()
{
    return G_SPEC_CONSTANTS_VAL;
}

struct PushConstants
{
    ulong VertexShaderConstants;
    ulong PixelShaderConstants;
    ulong SharedConstants;
};

#ifdef GTA4_RECOMP
#define GetTextureLodBias(SLOT) (*(reinterpret_cast<device float*>(g_PushConstants.SharedConstants + 752 + (SLOT) * 4)))
#define g_Booleans (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 528)))
#define g_SwappedTexcoords (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 532)))
#define g_SwappedNormals (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 536)))
#define g_SwappedBinormals (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 540)))
#define g_SwappedTangents (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 544)))
#define g_SwappedBlendWeights (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 548)))
#define g_HalfPixelOffset (*(reinterpret_cast<device float2*>(g_PushConstants.SharedConstants + 552)))
#define g_ClipPlane (*(reinterpret_cast<device float4*>(g_PushConstants.SharedConstants + 560)))
#define g_ClipPlaneEnabled (*(reinterpret_cast<device bool*>(g_PushConstants.SharedConstants + 576)))
#define g_AlphaThreshold (*(reinterpret_cast<device float*>(g_PushConstants.SharedConstants + 580)))
#define g_conditionalSurveyIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 584)))
#define g_conditionalRenderingIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 588)))
#else
#define g_Booleans (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 320)))
#define g_SwappedTexcoords (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 324)))
#define g_SwappedNormals (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 328)))
#define g_SwappedBinormals (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 332)))
#define g_SwappedTangents (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 336)))
#define g_SwappedBlendWeights (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 340)))
#define g_HalfPixelOffset (*(reinterpret_cast<device float2*>(g_PushConstants.SharedConstants + 344)))
#define g_ClipPlane (*(reinterpret_cast<device float4*>(g_PushConstants.SharedConstants + 352)))
#define g_ClipPlaneEnabled (*(reinterpret_cast<device bool*>(g_PushConstants.SharedConstants + 368)))
#define g_AlphaThreshold (*(reinterpret_cast<device float*>(g_PushConstants.SharedConstants + 372)))
#define g_conditionalSurveyIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 376)))
#define g_conditionalRenderingIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 380)))
#endif

#else

#ifdef GTA4_RECOMP
#define GetTextureLodBias(SLOT) g_TextureLodBias[(SLOT) / 4][(SLOT) % 4]
#define DEFINE_SHARED_CONSTANTS() \
    uint g_Booleans : packoffset(c33.x); \
    uint g_SwappedTexcoords : packoffset(c33.y); \
    uint g_SwappedNormals : packoffset(c33.z); \
    uint g_SwappedBinormals : packoffset(c33.w); \
    uint g_SwappedTangents : packoffset(c34.x);  \
    uint g_SwappedBlendWeights : packoffset(c34.y); \
    float2 g_HalfPixelOffset : packoffset(c34.z); \
    float4 g_ClipPlane : packoffset(c35.x); \
    bool g_ClipPlaneEnabled : packoffset(c36.x); \
    float g_AlphaThreshold : packoffset(c36.y); \
    uint g_conditionalSurveyIndex : packoffset(c36.z); \
    uint g_conditionalRenderingIndex : packoffset(c36.w); \
    float4 g_TextureLodBias[7] : packoffset(c47);
#else
#define DEFINE_SHARED_CONSTANTS() \
    uint g_Booleans : packoffset(c20.x); \
    uint g_SwappedTexcoords : packoffset(c20.y); \
    uint g_SwappedNormals : packoffset(c20.z); \
    uint g_SwappedBinormals : packoffset(c20.w); \
    uint g_SwappedTangents : packoffset(c21.x);  \
    uint g_SwappedBlendWeights : packoffset(c21.y); \
    float2 g_HalfPixelOffset : packoffset(c21.z); \
    float4 g_ClipPlane : packoffset(c22.x); \
    bool g_ClipPlaneEnabled : packoffset(c23.x); \
    float g_AlphaThreshold : packoffset(c23.y); \
    uint g_conditionalSurveyIndex : packoffset(c23.z); \
    uint g_conditionalRenderingIndex : packoffset(c23.w);
#endif

uint g_SpecConstants();

#endif

#ifndef GTA4_RECOMP
#define GetTextureLodBias(SLOT) 0.0
#endif

#if defined(GTA4_RECOMP) && defined(__spirv__)
uint ComputeXenosAlphaToMask(float alpha, float2 position, uint alphaToMask,
                             uint sampleCount)
{
    if ((alphaToMask & 0x100u) == 0u)
        return 0xFFFFFFFFu;

    uint offsetIndex = (uint(position.x) & 1u) | ((uint(position.y) & 1u) << 1u);
    uint offset = (alphaToMask >> (offsetIndex << 1u)) & 3u;
    float thresholdOffset = float(offset);
    uint sampleMask = 0u;

    if (sampleCount == 1u)
    {
        if (alpha >= 1.0 - thresholdOffset * 0.25)
            sampleMask |= 1u;
    }
    else if (sampleCount == 2u)
    {
        // Xenos top and bottom samples map to Vulkan samples 1 and 0.
        if (alpha >= 0.5 - thresholdOffset * 0.125)
            sampleMask |= 2u;
        if (alpha >= 1.0 - thresholdOffset * 0.125)
            sampleMask |= 1u;
    }
    else if (sampleCount == 4u)
    {
        // Xenos TL, BL, TR, BR map to Vulkan TL, TR, BL, BR.
        if (alpha >= 0.75 - thresholdOffset * 0.0625)
            sampleMask |= 1u;
        if (alpha >= 0.25 - thresholdOffset * 0.0625)
            sampleMask |= 4u;
        if (alpha >= 0.5 - thresholdOffset * 0.0625)
            sampleMask |= 2u;
        if (alpha >= 1.0 - thresholdOffset * 0.0625)
            sampleMask |= 8u;
    }
    else
    {
        return 0xFFFFFFFFu;
    }

    return sampleMask;
}
#endif

float4 cube(float4 value)
{
    float3 src = value.zwx;
    float3 abs_src = abs(src);

    float sc, tc, ma, id;

    if (abs_src.z >= abs_src.x && abs_src.z >= abs_src.y)
    {
        // Z major axis
        tc = -src.y;
        sc = src.z < 0.0 ? -src.x : src.x;
        ma = 2.0 * src.z;
        id = src.z < 0.0 ? 5.0 : 4.0;
    }
    else if (abs_src.y >= abs_src.x)
    {
        // Y major axis
        tc = src.y < 0.0 ? -src.z : src.z;
        sc = src.x;
        ma = 2.0 * src.y;
        id = src.y < 0.0 ? 3.0 : 2.0;
    }
    else
    {
        // X major axis
        tc = -src.y;
        sc = src.x < 0.0 ? src.z : -src.z;
        ma = 2.0 * src.x;
        id = src.x < 0.0 ? 1.0 : 0.0;
    }

    // Return as per Xbox 360 cube instruction output format:
    // x = t coordinate
    // y = s coordinate
    // z = 2 * major axis
    // w = face ID
    return float4(tc, sc, ma, id);
}

float3 cubeDir(float3 texCoord)
{
    // Move from 1...2 to -1...1
    float sc = (texCoord.x * 2.0) - 3.0;
    float tc = (texCoord.y * 2.0) - 3.0;

    uint face = uint(clamp(texCoord.z, 0.0, 5.0));

    // Split face into axis and sign
    uint axis = face >> 1;
    uint neg = face & 1;

    float3 dir;

    switch(axis)
    {
    case 0: // X major axis
        dir.y = -tc;
        dir.z = neg ? sc : -sc;
        dir.x = neg ? -1.0 : 1.0;
        break;

    case 1: // Y major axis
        dir.x = sc;
        dir.z = neg ? -tc : tc;
        dir.y = neg ? -1.0 : 1.0;
        break;

    default: // Z major axis
        dir.x = neg ? -sc : sc;
        dir.y = -tc;
        dir.z = neg ? -1.0 : 1.0;
        break;
    }

    return dir;
}

#ifdef __air__

struct Texture2DDescriptorHeap
{
    texture2d<float> tex;
};

struct Texture2DArrayDescriptorHeap
{
    texture2d_array<float> tex;
};

struct Texture3DDescriptorHeap
{
    texture3d<float> tex;
};

struct TextureCubeDescriptorHeap
{
    texturecube<float> tex;
};

struct SamplerDescriptorHeap
{
    sampler samp;
};

struct AtomicUintBuffer
{
    device atomic_uint* buffer;
};

uint2 getTexture2DDimensions(texture2d<float> texture)
{
    return uint2(texture.get_width(), texture.get_height());
}

uint3 getTexture2DArrayDimensions(texture2d_array<float> texture)
{
    return uint3(texture.get_width(), texture.get_height(), texture.get_array_size());
}

uint3 getTexture3DDimensions(texture3d<float> texture)
{
    return uint3(texture.get_width(), texture.get_height(), texture.get_depth());
}

float4 tfetch2D(constant Texture2DDescriptorHeap* textureHeap,
                constant SamplerDescriptorHeap* samplerHeap,
                uint resourceDescriptorIndex,
                uint samplerDescriptorIndex,
                float2 texCoord, float2 offset, float lodBias = 0.0)
{
    texture2d<float> texture = textureHeap[resourceDescriptorIndex].tex;
    sampler sampler = samplerHeap[samplerDescriptorIndex].samp;
    return texture.sample(sampler, texCoord + offset / (float2)getTexture2DDimensions(texture), bias(lodBias));
}

float4 tfetch2DArray(constant Texture2DArrayDescriptorHeap* textureHeap,
                     constant SamplerDescriptorHeap* samplerHeap,
                     uint resourceDescriptorIndex,
                     uint samplerDescriptorIndex,
                     float3 texCoord, float3 offset, float lodBias = 0.0)
{
    texture2d_array<float> texture = textureHeap[resourceDescriptorIndex].tex;
    sampler sampler = samplerHeap[samplerDescriptorIndex].samp;
    uint3 dimensions = getTexture2DArrayDimensions(texture);
    return texture.sample(sampler, texCoord.xy + offset.xy / float2(dimensions.xy), uint(texCoord.z * dimensions.z), bias(lodBias));
}

float4 tfetch3D(constant Texture3DDescriptorHeap* textureHeap,
                constant SamplerDescriptorHeap* samplerHeap,
                uint resourceDescriptorIndex,
                uint samplerDescriptorIndex,
                float3 texCoord, float3 offset, float lodBias = 0.0)
{
    texture3d<float> texture = textureHeap[resourceDescriptorIndex].tex;
    sampler sampler = samplerHeap[samplerDescriptorIndex].samp;
    return texture.sample(sampler, texCoord + offset / float3(getTexture3DDimensions(texture)), bias(lodBias));
}

float4 tfetchCube(constant TextureCubeDescriptorHeap* textureHeap,
                  constant SamplerDescriptorHeap* samplerHeap,
                  uint resourceDescriptorIndex,
                  uint samplerDescriptorIndex,
                  float3 texCoord, float lodBias = 0.0)
{
    texturecube<float> texture = textureHeap[resourceDescriptorIndex].tex;
    sampler sampler = samplerHeap[samplerDescriptorIndex].samp;
    float3 dir = cubeDir(texCoord);
    return texture.sample(sampler, dir, bias(lodBias));
}

float2 getWeights2D(constant Texture2DDescriptorHeap* textureHeap,
                    constant SamplerDescriptorHeap* samplerHeap,
                    uint resourceDescriptorIndex,
                    uint samplerDescriptorIndex,
                    float2 texCoord, float2 offset)
{
    texture2d<float> texture = textureHeap[resourceDescriptorIndex].tex;
    return select(fract(texCoord * float2(getTexture2DDimensions(texture)) + offset - 0.5), 0.0, isnan(texCoord));
}

float3 getWeights2DArray(constant Texture2DArrayDescriptorHeap* textureHeap,
                         constant SamplerDescriptorHeap* samplerHeap,
                         uint resourceDescriptorIndex,
                         uint samplerDescriptorIndex,
                         float3 texCoord, float3 offset)
{
    texture2d_array<float> texture = textureHeap[resourceDescriptorIndex].tex;
    return select(fract(texCoord * float3(getTexture2DArrayDimensions(texture)) + offset - 0.5), 0.0, isnan(texCoord));
}

float3 getWeights3D(constant Texture3DDescriptorHeap* textureHeap,
                    constant SamplerDescriptorHeap* samplerHeap,
                    uint resourceDescriptorIndex,
                    uint samplerDescriptorIndex,
                    float3 texCoord, float3 offset)
{
    texture3d<float> texture = textureHeap[resourceDescriptorIndex].tex;
    return select(fract(texCoord * float3(getTexture3DDimensions(texture)) + offset - 0.5),
                  0.0, isnan(texCoord));
}

#else

Texture2D<float4> g_Texture2DDescriptorHeap[] : register(t0, space0);
Texture2DArray<float4> g_Texture2DArrayDescriptorHeap[] : register(t0, space1);
Texture3D<float4> g_Texture3DDescriptorHeap[] : register(t0, space2);
TextureCube<float4> g_TextureCubeDescriptorHeap[] : register(t0, space3);
SamplerState g_SamplerDescriptorHeap[] : register(s0, space4);

#ifdef MARATHON_RECOMP
RWStructuredBuffer<uint> g_ConditionalSurveyBuffer : register(u0, space5);
#endif

uint2 getTexture2DDimensions(Texture2D<float4> texture)
{
    uint2 dimensions;
    texture.GetDimensions(dimensions.x, dimensions.y);
    return dimensions;
}

uint3 getTexture2DArrayDimensions(Texture2DArray<float4> texture)
{
    uint4 dimensions;
    texture.GetDimensions(0, dimensions.x, dimensions.y, dimensions.z, dimensions.w);
    return dimensions.xyz;
}

uint3 getTexture3DDimensions(Texture3D<float4> texture)
{
    uint3 dimensions;
    texture.GetDimensions(dimensions.x, dimensions.y, dimensions.z);
    return dimensions;
}

// Pixel shaders need implicit derivatives for authored mip selection,
// trilinear filtering and anisotropic footprints. Vertex shaders still use
// explicit level zero because implicit derivatives are not available there.
// Vulkan applies sampler bias after either explicit or implicit base LOD;
// the host has already clamped lodBias to the device sampler-bias limit.
float4 tfetch2D(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float2 texCoord, float2 offset, float lodBias = 0.0)
{
    Texture2D<float4> texture = g_Texture2DDescriptorHeap[resourceDescriptorIndex];
#ifdef XENOS_RECOMP_PIXEL_SHADER
    return texture.SampleBias(g_SamplerDescriptorHeap[samplerDescriptorIndex], texCoord + offset / getTexture2DDimensions(texture), lodBias);
#else
    return texture.SampleLevel(g_SamplerDescriptorHeap[samplerDescriptorIndex], texCoord + offset / getTexture2DDimensions(texture), 0.0 + lodBias);
#endif
}

float4 tfetch2DArray(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float3 texCoord, float3 offset, float lodBias = 0.0)
{
    Texture2DArray<float4> texture = g_Texture2DArrayDescriptorHeap[resourceDescriptorIndex];
    uint3 dimensions = getTexture2DArrayDimensions(texture);
#ifdef XENOS_RECOMP_PIXEL_SHADER
    return texture.SampleBias(g_SamplerDescriptorHeap[samplerDescriptorIndex], float3(texCoord.xy + offset.xy / dimensions.xy, texCoord.z * dimensions.z), lodBias);
#else
    return texture.SampleLevel(g_SamplerDescriptorHeap[samplerDescriptorIndex], float3(texCoord.xy + offset.xy / dimensions.xy, texCoord.z * dimensions.z), 0.0 + lodBias);
#endif
}

float4 tfetch3D(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float3 texCoord, float3 offset, float lodBias = 0.0)
{
    Texture3D<float4> texture = g_Texture3DDescriptorHeap[resourceDescriptorIndex];
    uint3 dimensions = getTexture3DDimensions(texture);
#ifdef XENOS_RECOMP_PIXEL_SHADER
    return texture.SampleBias(g_SamplerDescriptorHeap[samplerDescriptorIndex],
                          texCoord + offset / dimensions, lodBias);
#else
    return texture.SampleLevel(g_SamplerDescriptorHeap[samplerDescriptorIndex],
                               texCoord + offset / dimensions, 0.0 + lodBias);
#endif
}

float4 tfetchCube(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float3 texCoord, float lodBias = 0.0)
{
    float3 dir = cubeDir(texCoord);
#ifdef XENOS_RECOMP_PIXEL_SHADER
    return g_TextureCubeDescriptorHeap[resourceDescriptorIndex].SampleBias(
        g_SamplerDescriptorHeap[samplerDescriptorIndex], dir, lodBias);
#else
    return g_TextureCubeDescriptorHeap[resourceDescriptorIndex].SampleLevel(
        g_SamplerDescriptorHeap[samplerDescriptorIndex], dir, 0.0 + lodBias);
#endif
}

float2 getWeights2D(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float2 texCoord, float2 offset)
{
    Texture2D<float4> texture = g_Texture2DDescriptorHeap[resourceDescriptorIndex];
    return select(isnan(texCoord), 0.0, frac(texCoord * getTexture2DDimensions(texture) + offset - 0.5));
}

float3 getWeights2DArray(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float3 texCoord, float3 offset)
{
    Texture2DArray<float4> texture = g_Texture2DArrayDescriptorHeap[resourceDescriptorIndex];
    return select(isnan(texCoord), 0.0, frac(texCoord * getTexture2DArrayDimensions(texture) + offset - 0.5));
}

float3 getWeights3D(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float3 texCoord, float3 offset)
{
    Texture3D<float4> texture = g_Texture3DDescriptorHeap[resourceDescriptorIndex];
    return select(isnan(texCoord), 0.0,
                  frac(texCoord * getTexture3DDimensions(texture) + offset - 0.5));
}

#endif

#ifdef __air__
#define selectWrapper(a, b, c) select(c, b, a)
#else
#define selectWrapper(a, b, c) select(a, b, c)
#endif

#ifdef __air__
#define frac(X) fract(X)

template<typename T>
void clip(T a)
{
    if (a < 0.0) {
        discard_fragment();
    }
}

template<typename T>
float rcp(T a)
{
    return 1.0 / a;
}

template<typename T>
float4x4 mul(T a, T b)
{
    return b * a;
}
#endif

#ifdef __air__
#define UNROLL
#define BRANCH
#else
#define UNROLL [unroll]
#define BRANCH [branch]
#endif

float w0(float a)
{
    return (1.0f / 6.0f) * (a * (a * (-a + 3.0f) - 3.0f) + 1.0f);
}

float w1(float a)
{
    return (1.0f / 6.0f) * (a * a * (3.0f * a - 6.0f) + 4.0f);
}

float w2(float a)
{
    return (1.0f / 6.0f) * (a * (a * (-3.0f * a + 3.0f) + 3.0f) + 1.0f);
}

float w3(float a)
{
    return (1.0f / 6.0f) * (a * a * a);
}

float g0(float a)
{
    return w0(a) + w1(a);
}

float g1(float a)
{
    return w2(a) + w3(a);
}

float h0(float a)
{
    return -1.0f + w1(a) / (w0(a) + w1(a)) + 0.5f;
}

float h1(float a)
{
    return 1.0f + w3(a) / (w2(a) + w3(a)) + 0.5f;
}

#ifdef __air__

float4 tfetch2DBicubic(constant Texture2DDescriptorHeap* textureHeap,
                       constant SamplerDescriptorHeap* samplerHeap,
                       uint resourceDescriptorIndex,
                       uint samplerDescriptorIndex,
                       float2 texCoord, float2 offset, float lodBias = 0.0)
{
    texture2d<float> texture = textureHeap[resourceDescriptorIndex].tex;
    sampler sampler = samplerHeap[samplerDescriptorIndex].samp;
    uint2 dimensions = getTexture2DDimensions(texture);

    float x = texCoord.x * dimensions.x + offset.x;
    float y = texCoord.y * dimensions.y + offset.y;

    x -= 0.5f;
    y -= 0.5f;
    float px = floor(x);
    float py = floor(y);
    float fx = x - px;
    float fy = y - py;

    float g0x = g0(fx);
    float g1x = g1(fx);
    float h0x = h0(fx);
    float h1x = h1(fx);
    float h0y = h0(fy);
    float h1y = h1(fy);

    float4 r =
        g0(fy) * (g0x * texture.sample(sampler, float2(px + h0x, py + h0y) / float2(dimensions), bias(lodBias)) +
              g1x * texture.sample(sampler, float2(px + h1x, py + h0y) / float2(dimensions), bias(lodBias))) +
        g1(fy) * (g0x * texture.sample(sampler, float2(px + h0x, py + h1y) / float2(dimensions), bias(lodBias)) +
              g1x * texture.sample(sampler, float2(px + h1x, py + h1y) / float2(dimensions), bias(lodBias)));

    return r;
}

#else

float4 tfetch2DBicubic(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float2 texCoord, float2 offset, float lodBias = 0.0)
{
    Texture2D<float4> texture = g_Texture2DDescriptorHeap[resourceDescriptorIndex];
    SamplerState samplerState = g_SamplerDescriptorHeap[samplerDescriptorIndex];
    uint2 dimensions = getTexture2DDimensions(texture);

    float x = texCoord.x * dimensions.x + offset.x;
    float y = texCoord.y * dimensions.y + offset.y;

    x -= 0.5f;
    y -= 0.5f;
    float px = floor(x);
    float py = floor(y);
    float fx = x - px;
    float fy = y - py;

    float g0x = g0(fx);
    float g1x = g1(fx);
    float h0x = h0(fx);
    float h1x = h1(fx);
    float h0y = h0(fy);
    float h1y = h1(fy);

    float4 r =
        g0(fy) * (g0x * texture.SampleBias(samplerState, float2(px + h0x, py + h0y) / float2(dimensions), lodBias) +
            g1x * texture.SampleBias(samplerState, float2(px + h1x, py + h0y) / float2(dimensions), lodBias)) +
        g1(fy) * (g0x * texture.SampleBias(samplerState, float2(px + h0x, py + h1y) / float2(dimensions), lodBias) +
            g1x * texture.SampleBias(samplerState, float2(px + h1x, py + h1y) / float2(dimensions), lodBias));

    return r;
}

#endif

float4 tfetchR11G11B10(uint4 value)
{
    if (g_SpecConstants() & SPEC_CONSTANT_R11G11B10_NORMAL)
    {
        return float4(
            (value.x & 0x00000400 ? -1.0 : 0.0) + ((value.x & 0x3FF) / 1024.0),
            (value.x & 0x00200000 ? -1.0 : 0.0) + (((value.x >> 11) & 0x3FF) / 1024.0),
            (value.x & 0x80000000 ? -1.0 : 0.0) + (((value.x >> 22) & 0x1FF) / 512.0),
            0.0);
    }
    else
    {
#ifdef __air__
        return as_type<float4>(value);
#else
        return asfloat(value);
#endif
    }
}

float4 swapFloats(uint swappedFloats, float4 value, uint semanticIndex)
{
    return (swappedFloats & (1ull << semanticIndex)) != 0 ? value.yxwz : value;
}

float4 dst(float4 src0, float4 src1)
{
    float4 dest;
    dest.x = 1.0;
    dest.y = src0.y * src1.y;
    dest.z = src0.z;
    dest.w = src1.w;
    return dest;
}

float4 max4(float4 src0)
{
    return max(max(src0.x, src0.y), max(src0.z, src0.w));
}

#ifdef __air__

float2 getPixelCoord(constant Texture2DDescriptorHeap* textureHeap,
                     uint resourceDescriptorIndex,
                     float2 texCoord)
{
    texture2d<float> texture = textureHeap[resourceDescriptorIndex].tex;
    return (float2)getTexture2DDimensions(texture) * texCoord;
}

#else

float2 getPixelCoord(uint resourceDescriptorIndex, float2 texCoord)
{
    return getTexture2DDimensions(g_Texture2DDescriptorHeap[resourceDescriptorIndex]) * texCoord;
}

#endif

float computeMipLevel(float2 pixelCoord)
{
#ifdef __air__
    float2 dx = dfdx(pixelCoord);
    float2 dy = dfdy(pixelCoord);
#else
    float2 dx = ddx(pixelCoord);
    float2 dy = ddy(pixelCoord);
#endif
    float deltaMaxSqr = max(dot(dx, dx), dot(dy, dy));
    return max(0.0, 0.5 * log2(deltaMaxSqr));
}

#ifdef __air__

uint atomicLoadUint(device AtomicUintBuffer* buffer, uint index)
{
    return atomic_load_explicit(&buffer->buffer[index], memory_order_relaxed);
}

uint atomicFetchAddUint(device AtomicUintBuffer* buffer, uint index, uint value)
{
    return atomic_fetch_add_explicit(&buffer->buffer[index], value, memory_order_relaxed);
}

#else

uint atomicLoadUint(RWStructuredBuffer<uint> buffer, uint index)
{
    return buffer[index];
}

uint atomicFetchAddUint(RWStructuredBuffer<uint> buffer, uint index, uint value)
{
    uint originalValue;
    InterlockedAdd(buffer[index], value, originalValue);
    return originalValue;
}

#endif

#endif

#endif

#ifdef __spirv__

#define BumpSampler_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 4)
#define BumpSampler_Texture2DArrayDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 108)
#define BumpSampler_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 212)
#define BumpSampler_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 316)
#define BumpSampler_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 420)
#define DiskBrakeGlow vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3344, 0x10)
#define LuminanceConstants vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3424, 0x10)
#define SpecSampler_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 8)
#define SpecSampler_Texture2DArrayDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 112)
#define SpecSampler_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 216)
#define SpecSampler_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 320)
#define SpecSampler_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 424)
#define TextureSampler_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 0)
#define TextureSampler_Texture2DArrayDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 104)
#define TextureSampler_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 208)
#define TextureSampler_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 312)
#define TextureSampler_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 416)
#define bumpiness vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3408, 0x10)
#define gDepthFxParams vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 256, 0x10)
#define gDirectionalColour vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 288, 0x10)
#define gDirectionalLight vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 272, 0x10)
#define gInvColorExpBias vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 736, 0x10)
#define gLightAmbient0 vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 592, 0x10)
#define gLightAmbient1 vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 608, 0x10)
#define gLightColB vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 496, 0x10)
#define gLightColG vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 480, 0x10)
#define gLightColR vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 464, 0x10)
#define gLightConeOffset vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 432, 0x10)
#define gLightConeOffset2 vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1136, 0x10)
#define gLightConeScale vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 416, 0x10)
#define gLightConeScale2 vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1120, 0x10)
#define gLightDir2X vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1072, 0x10)
#define gLightDir2Y vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1088, 0x10)
#define gLightDir2Z vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1104, 0x10)
#define gLightDirX vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 352, 0x10)
#define gLightDirY vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 368, 0x10)
#define gLightDirZ vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 384, 0x10)
#define gLightFallOff vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 400, 0x10)
#define gLightPointColB vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1040, 0x10)
#define gLightPointColG vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1024, 0x10)
#define gLightPointColR vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 560, 0x10)
#define gLightPointFallOff vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 576, 0x10)
#define gLightPointPosX vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 512, 0x10)
#define gLightPointPosY vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 528, 0x10)
#define gLightPointPosZ vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 544, 0x10)
#define gLightPosX vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 304, 0x10)
#define gLightPosY vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 320, 0x10)
#define gLightPosZ vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 336, 0x10)
#define gShadowMatrix(INDEX) selectWrapper((INDEX) < 164, vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + (60 + min(INDEX, 163)) * 16, 0x10), 0.0)
#define gShadowParam0123 vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 912, 0x10)
#define gShadowZSamplerDir_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 60)
#define gShadowZSamplerDir_Texture2DArrayDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 164)
#define gShadowZSamplerDir_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 268)
#define gShadowZSamplerDir_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 372)
#define gShadowZSamplerDir_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 476)
#define globalFogColor vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 672, 0x10)
#define globalFogColorN vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 688, 0x10)
#define globalFogParams vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 656, 0x10)
#define globalScalars vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 624, 0x10)
#define globalScreenSize vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 704, 0x10)
#define matDiffuseColor vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3328, 0x10)
#define specMapIntMask vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3392, 0x10)
#define specularColorFactor vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3376, 0x10)
#define specularFactor vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 3360, 0x10)

#elif defined(__air__)

#define BumpSampler_Texture2DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 4)))
#define BumpSampler_Texture2DArrayDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 108)))
#define BumpSampler_Texture3DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 212)))
#define BumpSampler_TextureCubeDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 316)))
#define BumpSampler_SamplerDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 420)))
#define DiskBrakeGlow (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 3344)))
#define LuminanceConstants (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 3424)))
#define SpecSampler_Texture2DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 8)))
#define SpecSampler_Texture2DArrayDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 112)))
#define SpecSampler_Texture3DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 216)))
#define SpecSampler_TextureCubeDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 320)))
#define SpecSampler_SamplerDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 424)))
#define TextureSampler_Texture2DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 0)))
#define TextureSampler_Texture2DArrayDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 104)))
#define TextureSampler_Texture3DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 208)))
#define TextureSampler_TextureCubeDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 312)))
#define TextureSampler_SamplerDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 416)))
#define bumpiness (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 3408)))
#define gDepthFxParams (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 256)))
#define gDirectionalColour (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 288)))
#define gDirectionalLight (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 272)))
#define gInvColorExpBias (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 736)))
#define gLightAmbient0 (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 592)))
#define gLightAmbient1 (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 608)))
#define gLightColB (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 496)))
#define gLightColG (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 480)))
#define gLightColR (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 464)))
#define gLightConeOffset (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 432)))
#define gLightConeOffset2 (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1136)))
#define gLightConeScale (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 416)))
#define gLightConeScale2 (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1120)))
#define gLightDir2X (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1072)))
#define gLightDir2Y (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1088)))
#define gLightDir2Z (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1104)))
#define gLightDirX (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 352)))
#define gLightDirY (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 368)))
#define gLightDirZ (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 384)))
#define gLightFallOff (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 400)))
#define gLightPointColB (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1040)))
#define gLightPointColG (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1024)))
#define gLightPointColR (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 560)))
#define gLightPointFallOff (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 576)))
#define gLightPointPosX (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 512)))
#define gLightPointPosY (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 528)))
#define gLightPointPosZ (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 544)))
#define gLightPosX (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 304)))
#define gLightPosY (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 320)))
#define gLightPosZ (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 336)))
#define gShadowMatrix(INDEX) selectWrapper((INDEX) < 164, (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + (60 + min((uint)(INDEX), (uint)163)) * 16))), 0.0)
#define gShadowParam0123 (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 912)))
#define gShadowZSamplerDir_Texture2DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 60)))
#define gShadowZSamplerDir_Texture2DArrayDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 164)))
#define gShadowZSamplerDir_Texture3DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 268)))
#define gShadowZSamplerDir_TextureCubeDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 372)))
#define gShadowZSamplerDir_SamplerDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 476)))
#define globalFogColor (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 672)))
#define globalFogColorN (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 688)))
#define globalFogParams (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 656)))
#define globalScalars (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 624)))
#define globalScreenSize (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 704)))
#define matDiffuseColor (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 3328)))
#define specMapIntMask (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 3392)))
#define specularColorFactor (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 3376)))
#define specularFactor (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 3360)))

#else

cbuffer PixelShaderConstants : register(b1, space4)
{
	float4 DiskBrakeGlow : packoffset(c209);
	float4 LuminanceConstants : packoffset(c214);
	float4 bumpiness : packoffset(c213);
	float4 gDepthFxParams : packoffset(c16);
	float4 gDirectionalColour : packoffset(c18);
	float4 gDirectionalLight : packoffset(c17);
	float4 gInvColorExpBias : packoffset(c46);
	float4 gLightAmbient0 : packoffset(c37);
	float4 gLightAmbient1 : packoffset(c38);
	float4 gLightColB : packoffset(c31);
	float4 gLightColG : packoffset(c30);
	float4 gLightColR : packoffset(c29);
	float4 gLightConeOffset : packoffset(c27);
	float4 gLightConeOffset2 : packoffset(c71);
	float4 gLightConeScale : packoffset(c26);
	float4 gLightConeScale2 : packoffset(c70);
	float4 gLightDir2X : packoffset(c67);
	float4 gLightDir2Y : packoffset(c68);
	float4 gLightDir2Z : packoffset(c69);
	float4 gLightDirX : packoffset(c22);
	float4 gLightDirY : packoffset(c23);
	float4 gLightDirZ : packoffset(c24);
	float4 gLightFallOff : packoffset(c25);
	float4 gLightPointColB : packoffset(c65);
	float4 gLightPointColG : packoffset(c64);
	float4 gLightPointColR : packoffset(c35);
	float4 gLightPointFallOff : packoffset(c36);
	float4 gLightPointPosX : packoffset(c32);
	float4 gLightPointPosY : packoffset(c33);
	float4 gLightPointPosZ : packoffset(c34);
	float4 gLightPosX : packoffset(c19);
	float4 gLightPosY : packoffset(c20);
	float4 gLightPosZ : packoffset(c21);
	float4 gShadowMatrix[4] : packoffset(c60);
#define gShadowMatrix(INDEX) selectWrapper((INDEX) < 164, gShadowMatrix[min(INDEX, 163)], 0.0)
	float4 gShadowParam0123 : packoffset(c57);
	float4 globalFogColor : packoffset(c42);
	float4 globalFogColorN : packoffset(c43);
	float4 globalFogParams : packoffset(c41);
	float4 globalScalars : packoffset(c39);
	float4 globalScreenSize : packoffset(c44);
	float4 matDiffuseColor : packoffset(c208);
	float4 specMapIntMask : packoffset(c212);
	float4 specularColorFactor : packoffset(c211);
	float4 specularFactor : packoffset(c210);
};

cbuffer SharedConstants : register(b2, space4)
{
	uint BumpSampler_Texture2DDescriptorIndex : packoffset(c0.y);
	uint BumpSampler_Texture2DArrayDescriptorIndex : packoffset(c6.w);
	uint BumpSampler_Texture3DDescriptorIndex : packoffset(c13.y);
	uint BumpSampler_TextureCubeDescriptorIndex : packoffset(c19.w);
	uint BumpSampler_SamplerDescriptorIndex : packoffset(c26.y);
	uint SpecSampler_Texture2DDescriptorIndex : packoffset(c0.z);
	uint SpecSampler_Texture2DArrayDescriptorIndex : packoffset(c7.x);
	uint SpecSampler_Texture3DDescriptorIndex : packoffset(c13.z);
	uint SpecSampler_TextureCubeDescriptorIndex : packoffset(c20.x);
	uint SpecSampler_SamplerDescriptorIndex : packoffset(c26.z);
	uint TextureSampler_Texture2DDescriptorIndex : packoffset(c0.x);
	uint TextureSampler_Texture2DArrayDescriptorIndex : packoffset(c6.z);
	uint TextureSampler_Texture3DDescriptorIndex : packoffset(c13.x);
	uint TextureSampler_TextureCubeDescriptorIndex : packoffset(c19.z);
	uint TextureSampler_SamplerDescriptorIndex : packoffset(c26.x);
	uint gShadowZSamplerDir_Texture2DDescriptorIndex : packoffset(c3.w);
	uint gShadowZSamplerDir_Texture2DArrayDescriptorIndex : packoffset(c10.y);
	uint gShadowZSamplerDir_Texture3DDescriptorIndex : packoffset(c16.w);
	uint gShadowZSamplerDir_TextureCubeDescriptorIndex : packoffset(c23.y);
	uint gShadowZSamplerDir_SamplerDescriptorIndex : packoffset(c29.w);
	DEFINE_SHARED_CONSTANTS();
};

#endif


#define FUSION_FOG_START_EXEMPTION
#include "../fusion_forward_fog.hlsli"
#undef globalFogParams
#define globalFogParams FusionForwardFogParameters()

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
	float4 c244 = as_type<float4>(uint4(0x0, 0x0, 0x0, 0x0));
#else
	float4 c244 = asfloat(uint4(0x0, 0x0, 0x0, 0x0));
#endif
#ifdef __air__
	float4 c245 = as_type<float4>(uint4(0x0, 0x0, 0x0, 0x0));
#else
	float4 c245 = asfloat(uint4(0x0, 0x0, 0x0, 0x0));
#endif
#ifdef __air__
	float4 c246 = as_type<float4>(uint4(0x0, 0x0, 0x0, 0x0));
#else
	float4 c246 = asfloat(uint4(0x0, 0x0, 0x0, 0x0));
#endif
#ifdef __air__
	float4 c247 = as_type<float4>(uint4(0x3FAAAAAB, 0x3E22F983, 0x3F8E38E4, 0x37800000));
#else
	float4 c247 = asfloat(uint4(0x3FAAAAAB, 0x3E22F983, 0x3F8E38E4, 0x37800000));
#endif
#ifdef __air__
	float4 c248 = as_type<float4>(uint4(0xBDCCCCCD, 0x0, 0x40800000, 0x3E800000));
#else
	float4 c248 = asfloat(uint4(0xBDCCCCCD, 0x0, 0x40800000, 0x3E800000));
#endif
#ifdef __air__
	float4 c249 = as_type<float4>(uint4(0x33D6BF95, 0x3F800000, 0x40C90FDB, 0x3FF33333));
#else
	float4 c249 = asfloat(uint4(0x33D6BF95, 0x3F800000, 0x40C90FDB, 0x3FF33333));
#endif
#ifdef __air__
	float4 c250 = as_type<float4>(uint4(0x40400000, 0x40E46A7F, 0x3F000000, 0x43800000));
#else
	float4 c250 = asfloat(uint4(0x40400000, 0x40E46A7F, 0x3F000000, 0x43800000));
#endif
#ifdef __air__
	float4 c251 = as_type<float4>(uint4(0x41C80000, 0x42C40000, 0xC0490FDB, 0x3F000000));
#else
	float4 c251 = asfloat(uint4(0x41C80000, 0x42C40000, 0xC0490FDB, 0x3F000000));
#endif
#ifdef __air__
	float4 c252 = as_type<float4>(uint4(0x3A19999A, 0x3A99999A, 0x3B19999A, 0x3AE66667));
#else
	float4 c252 = asfloat(uint4(0x3A19999A, 0x3A99999A, 0x3B19999A, 0x3AE66667));
#endif
#ifdef __air__
	float4 c253 = as_type<float4>(uint4(0xBE800000, 0x38D1B717, 0x40000000, 0xBF000000));
#else
	float4 c253 = asfloat(uint4(0xBE800000, 0x38D1B717, 0x40000000, 0xBF000000));
#endif
#ifdef __air__
	float4 c254 = as_type<float4>(uint4(0x3D93A92A, 0x3E59999A, 0x3F372474, 0x3DCCCCCD));
#else
	float4 c254 = asfloat(uint4(0x3D93A92A, 0x3E59999A, 0x3F372474, 0x3DCCCCCD));
#endif
#ifdef __air__
	float4 c255 = as_type<float4>(uint4(0xBF800000, 0x0, 0x0, 0x0));
#else
	float4 c255 = asfloat(uint4(0xBF800000, 0x0, 0x0, 0x0));
#endif

	float4 r0 = input.iTexCoord0;
	float4 r1 = input.iTexCoord1;
	float4 r2 = input.iTexCoord2;
	float4 r3 = input.iTexCoord3;
	float4 r4 = input.iTexCoord4;
	float4 r5 = input.iTexCoord5;
	float4 r6 = input.iColor0;
	float4 r7 = float4((input.iPos.xy - 0.5) * float2(iFace ? 1.0 : -1.0, 1.0), 0.0, 0.0);
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
	r5.w = ps;
	r0.zw = (float2)((max(abs(r7.yx), abs(r7.yx))));
	r8.xyzw = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		TextureSampler_Texture2DDescriptorIndex, TextureSampler_SamplerDescriptorIndex, r0.xy, float2(0, 0)).xyzw;
	r7.xzw = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		BumpSampler_Texture2DDescriptorIndex, BumpSampler_SamplerDescriptorIndex, r0.xy, float2(0, 0)).wxy;
	r9.xyzw = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		SpecSampler_Texture2DDescriptorIndex, SpecSampler_SamplerDescriptorIndex, r0.xy, float2(0, 0)).xyzw;
	r3.w = (float)((dot(r9.zxy, specMapIntMask.zxy)));
	ps = c249.y - r7.x;
	r0.x = ps;
	r0.x = (float)((r7.z > r0.x));
	ps = r7.z - r7.x;
	r0.y = ps;
	r7.y = (float)((r0.y * r0.x + r7.x));
	r10.xy = (float2)((r7.yw + c253.ww));
	r0.x = (float)((dot(r7.yw, r7.yw) + c248.y));
	r7.xy = (float2)((r10.xy * bumpiness.xx));
	ps = c249.y - r0.x;
	r0.x = ps;
	r5.xyz = (float3)((r7.yyy * r5.zyx));
	ps = sqrt(abs(r0.x));
	r0.x = ps;
	r4.xyz = (float3)((r7.xxx * r4.zyx + r5.xyz));
	r4.xyw = (float3)((r0.xxx * r1.zyx + r4.xyz));
	r0.x = (float)((dot(r4.xwy, r4.xwy)));
	ps = max(r9.w, r9.w);
	r0.y = ps;
	r4.z = (float)((r3.w * specularColorFactor.x));
	ps = clamp(rsqrt(abs(r0.x)), -FLT_MAX, FLT_MAX);
	r0.x = ps;
	r4.xyw = (float3)((r4.wyx * r0.xxx));
	ps = specularFactor.x * r0.y;
	r0.x = ps;
	r5.xyz = (float3)((r8.xyz * matDiffuseColor.xyz));
	r3.w = (float)((dot(r3.zxy, r3.zxy)));
	ps = max(r8.w, r8.w);
	r0.y = (float)((dot(r6.zxy, LuminanceConstants.zxy)));
	ps = r6.w * ps;
	r6.w = ps;
	r6.xyz = (float3)((r5.zxy * r6.zxy));
	ps = clamp(rsqrt(abs(r3.w)), -FLT_MAX, FLT_MAX);
	r3.w = ps;
	r5.xyz = (float3)((r3.www * r3.xyz));
	ps = globalScalars.z * r0.y;
	r3.z = ps;
	r3.xyw = (float3)((max(r2.xyz, r2.xyz)));
	r0.y = (float)((max(r1.w, r1.w)));
	r1.x = (float)((globalScreenSize.y * c250.y));
	r2.xyz = (float3)((r3.yyy * gShadowMatrix(1).zxy));
	ps = gShadowMatrix(2).z * r3.w;
	r1.y = ps;
	r7.xyz = (float3)((r4.yyy * gShadowMatrix(1).zxy));
	ps = gShadowMatrix(2).x * r3.w;
	r1.z = ps;
	r9.yzw = (float3)((r4.xxx * gShadowMatrix(0).zxy + r7.xyz));
	r8.yzw = (float3)((r3.xxx * gShadowMatrix(0).zxy + r2.xyz));
	r0.zw = (float2)((r0.wz * globalScreenSize.zw));
	ps = gShadowMatrix(2).y * r3.w;
	r1.w = ps;
	r2.x = (float)((dot(-r5.zxy, r4.wxy)));
	ps = c250.x * r0.z;
	r0.z = ps;
	r1.x = (float)((r0.w * r1.x));
	ps = r2.x + r2.x;
	r0.w = ps;
	r7.xyz = (float3)((-r0.www * r4.wyx + -r5.zyx));
	r2.x = (float)((dot(r4.wxy, -gDirectionalLight.zxy)));
	ps = globalScreenSize.x * r0.z;
	r8.x = ps;
	r8.xyzw = (float4)((r8.xyzw + r1.xyzw));
	ps = gShadowMatrix(2).z * r4.w;
	r1.y = ps;
	r2.y = (float)((saturate(dot(r7.xzy, -gDirectionalLight.zxy))));
	ps = max(r8.x, r8.x);
	r0.z = ps;
	r8.yzw = (float3)((r8.yzw + gShadowMatrix(3).zxy));
	ps = c247.y * r0.z;
	r8.x = ps;
	r9.x = (float)((dot(r8.zw, r8.zw) + c248.y));
	r0.zw = (float2)((r8.xy + c250.zw));
	ps = gShadowMatrix(2).x * r4.w;
	r1.z = ps;
	r1.x = (float)((r0.w * r0.w));
	ps = gShadowMatrix(2).y * r4.w;
	r1.w = ps;
	r9.xyzw = (float4)((r9.yzwx + r1.yzwx));
	r9.w = (float)((saturate(r9.w * c247.w)));
	r0.z = (float)((frac(r0.z)));
	ps = sqrt(r9.w);
	r0.w = ps;
	r1.xy = (float2)((r0.wz * c249.wz));
	ps = c254.w + r1.x;
	r0.z = ps;
	r10.yzw = (float3)((r9.xyz * r0.zzz + r8.yzw));
	r0.zw = (float2)((r10.zw * gShadowParam0123.zz));
	ps = max(abs(r0.z), abs(r0.w));
	r2.z = ps;
	r11.xyz = (float3)((r2.xyz + c253.xyz));
	r1.zw = (float2)((r0.wz * c251.ww));
	ps = clamp(rcp(r11.z), -FLT_MAX, FLT_MAX);
	r0.z = ps;
	r1.zw = (float2)((r1.zw * r0.zz));
	r2.xyw = (float3)((r1.zwy + c251.wwz));
	ps = cos(r2.w);
	r0.z = ps;
	r2.z = (float)((-r2.x + c249.y));
	ps = sin(r2.w);
	r0.w = ps;
	r8.x = (float)((-r0.w * c252.z + r2.z));
	r8.yzw = (float3)((r0.zwz * c252.zww + r2.yyz));
	r1.x = (float)((r0.w * c252.y + r2.z));
	r1.yzw = (float3)((-r0.zwz * c252.yxx + r2.yyz));
	r2.x = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		gShadowZSamplerDir_Texture2DDescriptorIndex, gShadowZSamplerDir_SamplerDescriptorIndex, r8.yx, float2(0, 0)).x;
	r2.y = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		gShadowZSamplerDir_Texture2DDescriptorIndex, gShadowZSamplerDir_SamplerDescriptorIndex, r8.zw, float2(0, 0)).x;
	r2.z = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		gShadowZSamplerDir_Texture2DDescriptorIndex, gShadowZSamplerDir_SamplerDescriptorIndex, r1.zw, float2(0, 0)).x;
	r2.w = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		gShadowZSamplerDir_Texture2DDescriptorIndex, gShadowZSamplerDir_SamplerDescriptorIndex, r1.yx, float2(0, 0)).x;
	ps = c253.y + r0.x;
	r0.z = ps;
	r9.xyz = (float3)((gDirectionalColour.zyx * gDirectionalColour.www));
	ps = max(gShadowParam0123.w, gShadowParam0123.w);
	r0.w = (float)((saturate(r4.w * c253.w + c251.w)));
	r8.xyz = (float3)((r0.www * gLightAmbient1.zyx + gLightAmbient0.zyx));
	r1.x = (float)((saturate(r11.x * c247.x)));
	ps = c251.w * ps;
	r0.w = ps;
	r1.xyz = (float3)((r9.zyx * r1.xxx));
	ps = clamp(log2(abs(r11.y)), -FLT_MAX, FLT_MAX);
	r10.x = ps;
	r0.zw = (float2)((r0.wz * r10.yx));
	r2.xyzw = (float4)((r2.xyzw >= r0.zzzz));
	r0.z = (float)((dot(r2.zwxy, c248.wwww)));
	r2.y = (float)((-r0.z + c249.y));
	ps = exp2(r0.w);
	r2.x = ps;
	r2.xyzw = (float4)((r9.zyxw * r2.xxxy));
	r0.z = (float)((r0.z + r2.w));
	r1.xyz = (float3)((r1.xyz * r0.zzz));
	r2.xyz = (float3)((r2.xyz * r0.zzz));
	ps = c248.w * r0.x;
	r0.x = ps;
	r2.xyz = (float3)((r2.zxy * gDirectionalLight.www));
	p0 = r5.w > 0.0;
	ps = p0 ? 0.0 : 1.0;
	r1.xyz = (float3)((r8.xzy * r3.zzz + r1.zxy));
	if (p0)
	{
		r12.xyzw = (float4)((-r3.xxxx + gLightPosX.wzyx));
		r14.xyzw = (float4)((-r3.wwww + gLightPosZ.wzyx));
		r13.xyzw = (float4)((-r3.yyyy + gLightPosY.wzyx));
		ps = r14.x * r14.x;
		r10.x = ps;
		r11.xyzw = (float4)((r14.xyzw * r4.wwww));
		ps = r14.y * r14.y;
		r10.y = ps;
		r9.xyzw = (float4)((r14.xyzw * -gLightDirZ.wzyx));
		ps = r14.z * r14.z;
		r10.z = ps;
		r8.xyzw = (float4)((r7.xxxx * r14.wzyx));
		ps = r14.w * r14.w;
		r10.w = ps;
		r10.xyzw = (float4)((r13.xyzw * r13.xyzw + r10.xyzw));
		r8.xyzw = (float4)((r7.yyyy * r13.wzyx + r8.xyzw));
		r9.xyzw = (float4)((r13.wzyx * -gLightDirY.xyzw + r9.wzyx));
		r11.xyzw = (float4)((r13.xyzw * r4.yyyy + r11.xyzw));
		r11.xyzw = (float4)((r12.xyzw * r4.xxxx + r11.xyzw));
		r9.xyzw = (float4)((r12.xyzw * -gLightDirX.wzyx + r9.wzyx));
		r8.xyzw = (float4)((r7.zzzz * r12.xyzw + r8.wzyx));
		r10.xyzw = (float4)((r12.wzyx * r12.wzyx + r10.wzyx));
		r12.xyzw = (float4)((-r10.wzyx * gLightFallOff.wzyx + c249.yyyy));
		ps = clamp(rsqrt(abs(r10.x)), -FLT_MAX, FLT_MAX);
		r10.x = ps;
		ps = clamp(rsqrt(abs(r10.y)), -FLT_MAX, FLT_MAX);
		r10.y = ps;
		ps = clamp(rsqrt(abs(r10.z)), -FLT_MAX, FLT_MAX);
		r10.z = ps;
		ps = clamp(rsqrt(abs(r10.w)), -FLT_MAX, FLT_MAX);
		r10.w = ps;
		r8.xyzw = (float4)((r8.wzyx * r10.xyzw));
		r9.xyzw = (float4)((r9.wzyx * r10.xyzw));
		ps = clamp(log2(abs(r8.x)), -FLT_MAX, FLT_MAX);
		r8.x = ps;
		r9.xyzw = (float4)((saturate(r9.wzyx * gLightConeScale.wzyx + gLightConeOffset.wzyx)));
		r12.xyzw = (float4)((max(r12.xyzw, c248.yyyy)));
		ps = clamp(log2(abs(r8.y)), -FLT_MAX, FLT_MAX);
		r8.y = ps;
		r13.xyzw = (float4)((r12.xyzw * r12.xyzw));
		ps = clamp(log2(abs(r8.z)), -FLT_MAX, FLT_MAX);
		r8.z = ps;
		r13.xyzw = (float4)((r13.xyzw * r12.xyzw));
		ps = clamp(log2(abs(r8.w)), -FLT_MAX, FLT_MAX);
		r8.w = ps;
		r12.xyzw = (float4)((r13.xyzw * r12.xyzw + c248.xxxx));
		r8.xyzw = (float4)((r0.xxxx * r8.xyzw));
		r12.xyzw = (float4)((max(r12.xyzw, c248.yyyy)));
		r12.xyzw = (float4)((r12.xyzw * c247.zzzz));
		r11.xyzw = (float4)((r12.wzyx * r11.wzyx));
		r10.xyzw = (float4)((saturate(r11.xyzw * r10.xyzw)));
		r10.xyzw = (float4)((saturate(r10.wzyx * r9.xyzw)));
		ps = exp2(r8.x);
		r8.x = ps;
		r9.x = (float)((dot(r10.xywz, gLightColB.wzxy)));
		ps = exp2(r8.y);
		r8.y = ps;
		r9.y = (float)((dot(r10.xywz, gLightColG.wzxy)));
		ps = exp2(r8.z);
		r8.z = ps;
		r9.z = (float)((dot(r10.xywz, gLightColR.wzxy)));
		ps = exp2(r8.w);
		r8.w = ps;
		r10.xyzw = (float4)((r10.wzyx * r8.xyzw));
		r8.x = (float)((dot(r10.wzxy, gLightColB.wzxy)));
		r8.y = (float)((dot(r10.wzxy, gLightColG.wzxy)));
		r8.z = (float)((dot(r10.wzxy, gLightColR.wzxy)));
		r1.xyz = (float3)((r9.xzy + r1.xyz));
		r2.xyz = (float3)((r8.xzy + r2.xyz));
	}
	r0.z = (float)((r5.w > c248.z));
	p0 = r0.z != 0.0;
	ps = p0 ? 0.0 : 1.0;
	if (p0)
	{
		r10.xyzw = (float4)((-r3.xxxx + gLightPointPosX.wzyx));
		r13.xyzw = (float4)((-r3.wwww + gLightPointPosZ.wzyx));
		r12.xyzw = (float4)((-r3.yyyy + gLightPointPosY.wzyx));
		ps = r13.x * r13.x;
		r8.x = ps;
		r9.xyzw = (float4)((r13.xyzw * r4.wwww));
		ps = r13.y * r13.y;
		r8.y = ps;
		r11.xyzw = (float4)((r13.xyzw * -gLightDir2Z.wzyx));
		ps = r13.z * r13.z;
		r8.z = ps;
		r3.xyzw = (float4)((r7.xxxx * r13.wzyx));
		ps = r13.w * r13.w;
		r8.w = ps;
		r8.xyzw = (float4)((r12.xyzw * r12.xyzw + r8.xyzw));
		r3.xyzw = (float4)((r7.yyyy * r12.wzyx + r3.xyzw));
		r11.xyzw = (float4)((r12.wzyx * -gLightDir2Y.xyzw + r11.wzyx));
		r9.xyzw = (float4)((r12.xyzw * r4.yyyy + r9.xyzw));
		r9.xyzw = (float4)((r10.xyzw * r4.xxxx + r9.xyzw));
		r11.xyzw = (float4)((r10.xyzw * -gLightDir2X.wzyx + r11.wzyx));
		r3.xyzw = (float4)((r7.zzzz * r10.xyzw + r3.wzyx));
		r7.xyzw = (float4)((r10.xyzw * r10.xyzw + r8.xyzw));
		r10.xyzw = (float4)((-r7.xyzw * gLightPointFallOff.wzyx + c249.yyyy));
		ps = clamp(rsqrt(abs(r7.w)), -FLT_MAX, FLT_MAX);
		r8.x = ps;
		ps = clamp(rsqrt(abs(r7.z)), -FLT_MAX, FLT_MAX);
		r8.y = ps;
		ps = clamp(rsqrt(abs(r7.y)), -FLT_MAX, FLT_MAX);
		r8.z = ps;
		ps = clamp(rsqrt(abs(r7.x)), -FLT_MAX, FLT_MAX);
		r8.w = ps;
		r3.xyzw = (float4)((r3.wzyx * r8.xyzw));
		r7.xyzw = (float4)((r11.wzyx * r8.xyzw));
		ps = clamp(log2(abs(r3.x)), -FLT_MAX, FLT_MAX);
		r3.x = ps;
		r7.xyzw = (float4)((saturate(r7.wzyx * gLightConeScale2.wzyx + gLightConeOffset2.wzyx)));
		r10.xyzw = (float4)((max(r10.xyzw, c248.yyyy)));
		ps = clamp(log2(abs(r3.y)), -FLT_MAX, FLT_MAX);
		r3.y = ps;
		r11.xyzw = (float4)((r10.xyzw * r10.xyzw));
		ps = clamp(log2(abs(r3.z)), -FLT_MAX, FLT_MAX);
		r3.z = ps;
		r11.xyzw = (float4)((r11.xyzw * r10.xyzw));
		ps = clamp(log2(abs(r3.w)), -FLT_MAX, FLT_MAX);
		r3.w = ps;
		r10.xyzw = (float4)((r11.xyzw * r10.xyzw + c248.xxxx));
		r3.xyzw = (float4)((r0.xxxx * r3.xyzw));
		r10.xyzw = (float4)((max(r10.xyzw, c248.yyyy)));
		r10.xyzw = (float4)((r10.xyzw * c247.zzzz));
		r9.xyzw = (float4)((r10.wzyx * r9.wzyx));
		r8.xyzw = (float4)((saturate(r9.xyzw * r8.xyzw)));
		r8.xyzw = (float4)((saturate(r8.wzyx * r7.xyzw)));
		ps = exp2(r3.x);
		r7.x = ps;
		r3.x = (float)((dot(r8.xywz, gLightPointColB.wzxy)));
		ps = exp2(r3.y);
		r7.y = ps;
		r3.y = (float)((dot(r8.xywz, gLightPointColG.wzxy)));
		ps = exp2(r3.z);
		r7.z = ps;
		r3.z = (float)((dot(r8.xywz, gLightPointColR.wzxy)));
		ps = exp2(r3.w);
		r7.w = ps;
		r7.xyzw = (float4)((r8.wzyx * r7.xyzw));
		r0.x = (float)((dot(r7.wzxy, gLightPointColB.wzxy)));
		r0.z = (float)((dot(r7.wzxy, gLightPointColG.wzxy)));
		r0.w = (float)((dot(r7.wzxy, gLightPointColR.wzxy)));
		r1.xyz = (float3)((r3.xzy + r1.xyz));
		r2.xyz = (float3)((r0.xwz + r2.xyz));
	}
	ps = -globalFogParams.x - -r0.y;
	r7.z = ps;
	r1.w = (float)((gDepthFxParams.w + -gDepthFxParams.z));
	ps = gDepthFxParams.w - r0.y;
	r7.w = ps;
	r7.xy = (float2)((gDepthFxParams.yx + c255.xx));
	ps = max(-globalFogParams.w, -globalFogParams.w);
	r3.yzw = (float3)((globalFogColor.xyz + -globalFogColorN.xyz));
	ps = c249.y + ps;
	r3.x = ps;
	r0.z = (float)((globalScalars.x * gInvColorExpBias.x));
	ps = globalFogParams.y - globalFogParams.x;
	r0.x = ps;
	output.oC0.w = (float)((r0.z * r6.w));
	r0.z = (float)((dot(r5.zxy, r4.wxy)));
	ps = clamp(rcp(globalFogParams.x), -FLT_MAX, FLT_MAX);
	r0.w = ps;
	r0.y = (float)((saturate(r0.w * r0.y)));
	ps = clamp(rcp(r0.x), -FLT_MAX, FLT_MAX);
	r0.x = ps;
	r0.w = (float)((r0.y * globalFogParams.w + globalFogParams.z));
	r0.z = (float)((-abs(r0.z) + c249.y));
	ps = clamp(rcp(r1.w), -FLT_MAX, FLT_MAX);
	r0.y = ps;
	r0.xy = (float2)((saturate(r7.wz * r0.yx)));
	ps = r0.z * r0.z;
	r0.z = ps;
	r0.w = (float)((r0.y * r3.x + r0.w));
	r3.xyz = (float3)((r0.yyy * r3.yzw + globalFogColorN.xyz));
	ps = r0.z * r0.z;
	r0.z = ps;
	r0.z = (float)((saturate(r0.z * c251.w + c251.w)));
	r0.z = (float)((r0.z * r4.z));
	ps = c249.y - r0.x;
	r3.w = ps;
	r0.xyz = (float3)((r0.zzz * r2.xyz));
	ps = max(r7.y, r7.y);
	r4.x = (float)((r6.x * r1.x + r0.x));
	r0.xy = (float2)((r6.yz * r1.yz + r0.yz));
	r4.yz = (float2)((DiskBrakeGlow.xx * c251.yx + r0.xy));
	r0.x = (float)((dot(r4.xyz, c254.xyz)));
	ps = r3.w * ps;
	r0.y = ps;
	r7.zw = (float2)((r0.xy + c249.xy));
	r1.yzw = (float3)((-r0.xxx + r4.xyz));
	ps = clamp(log2(abs(r7.z)), -FLT_MAX, FLT_MAX);
	r0.z = ps;
	r1.x = (float)((r3.w * r0.z));
	r1.xyzw = (float4)((r1.yzwx * r7.wwwx));
	r1.xyz = (float3)((r0.xxx + r1.xyz));
	ps = exp2(r1.w);
	r0.x = ps;
	r0.xyz = (float3)((r1.xyz * r0.xxx));
	r1.xyz = (float3)((r3.zxy + -r0.xyz));
	output.oC0.xyz = (float3)((r1.yzx * r0.www + r0.yzx));
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
	output.oC0.rgb = FusionForwardFog(output.oC0.rgb, input.iPos);
	return output;
}