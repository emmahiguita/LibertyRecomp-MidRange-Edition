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

#define CloudBias vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1152, 0x10)
#define CloudColor vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1104, 0x10)
#define CloudFadeOut vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1168, 0x10)
#define CloudInscatteringRange vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1232, 0x10)
#define CloudShadowStrength vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1216, 0x10)
#define CloudThicknessEdgeSmoothDetailScaleStrength vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1248, 0x10)
#define CloudThreshold vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1136, 0x10)
#define GalaxySampler_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 4)
#define GalaxySampler_Texture2DArrayDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 108)
#define GalaxySampler_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 212)
#define GalaxySampler_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 316)
#define GalaxySampler_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 420)
#define HDRExposure vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1296, 0x10)
#define HDRExposureClamp vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1328, 0x10)
#define HDRSunExposure vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1312, 0x10)
#define HighDetailNoiseSampler_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 12)
#define HighDetailNoiseSampler_Texture2DArrayDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 116)
#define HighDetailNoiseSampler_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 220)
#define HighDetailNoiseSampler_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 324)
#define HighDetailNoiseSampler_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 428)
#define PerlinNoiseSampler_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 8)
#define PerlinNoiseSampler_Texture2DArrayDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 112)
#define PerlinNoiseSampler_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 216)
#define PerlinNoiseSampler_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 320)
#define PerlinNoiseSampler_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 424)
#define StarFieldBrightness vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1264, 0x10)
#define StarFieldSampler_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 0)
#define StarFieldSampler_Texture2DArrayDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 104)
#define StarFieldSampler_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 208)
#define StarFieldSampler_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 312)
#define StarFieldSampler_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 416)
#define SunCentre vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1024, 0x10)
#define SunColor vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1056, 0x10)
#define SunDirection vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1040, 0x10)
#define SunSize vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1280, 0x10)
#define SunsetColor vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1120, 0x10)
#define TopCloudBiasDetailThresholdHeight vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1184, 0x10)
#define TopCloudColor vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1200, 0x10)
#define gtaSkyDomeFade vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1072, 0x10)
#define gtaWaterColor vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 1088, 0x10)

#elif defined(__air__)

#define CloudBias (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1152)))
#define CloudColor (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1104)))
#define CloudFadeOut (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1168)))
#define CloudInscatteringRange (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1232)))
#define CloudShadowStrength (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1216)))
#define CloudThicknessEdgeSmoothDetailScaleStrength (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1248)))
#define CloudThreshold (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1136)))
#define GalaxySampler_Texture2DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 4)))
#define GalaxySampler_Texture2DArrayDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 108)))
#define GalaxySampler_Texture3DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 212)))
#define GalaxySampler_TextureCubeDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 316)))
#define GalaxySampler_SamplerDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 420)))
#define HDRExposure (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1296)))
#define HDRExposureClamp (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1328)))
#define HDRSunExposure (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1312)))
#define HighDetailNoiseSampler_Texture2DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 12)))
#define HighDetailNoiseSampler_Texture2DArrayDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 116)))
#define HighDetailNoiseSampler_Texture3DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 220)))
#define HighDetailNoiseSampler_TextureCubeDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 324)))
#define HighDetailNoiseSampler_SamplerDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 428)))
#define PerlinNoiseSampler_Texture2DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 8)))
#define PerlinNoiseSampler_Texture2DArrayDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 112)))
#define PerlinNoiseSampler_Texture3DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 216)))
#define PerlinNoiseSampler_TextureCubeDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 320)))
#define PerlinNoiseSampler_SamplerDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 424)))
#define StarFieldBrightness (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1264)))
#define StarFieldSampler_Texture2DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 0)))
#define StarFieldSampler_Texture2DArrayDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 104)))
#define StarFieldSampler_Texture3DDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 208)))
#define StarFieldSampler_TextureCubeDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 312)))
#define StarFieldSampler_SamplerDescriptorIndex (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 416)))
#define SunCentre (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1024)))
#define SunColor (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1056)))
#define SunDirection (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1040)))
#define SunSize (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1280)))
#define SunsetColor (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1120)))
#define TopCloudBiasDetailThresholdHeight (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1184)))
#define TopCloudColor (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1200)))
#define gtaSkyDomeFade (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1072)))
#define gtaWaterColor (*(reinterpret_cast<device float4*>(g_PushConstants.PixelShaderConstants + 1088)))

#else

cbuffer PixelShaderConstants : register(b1, space4)
{
	float4 CloudBias : packoffset(c72);
	float4 CloudColor : packoffset(c69);
	float4 CloudFadeOut : packoffset(c73);
	float4 CloudInscatteringRange : packoffset(c77);
	float4 CloudShadowStrength : packoffset(c76);
	float4 CloudThicknessEdgeSmoothDetailScaleStrength : packoffset(c78);
	float4 CloudThreshold : packoffset(c71);
	float4 HDRExposure : packoffset(c81);
	float4 HDRExposureClamp : packoffset(c83);
	float4 HDRSunExposure : packoffset(c82);
	float4 StarFieldBrightness : packoffset(c79);
	float4 SunCentre : packoffset(c64);
	float4 SunColor : packoffset(c66);
	float4 SunDirection : packoffset(c65);
	float4 SunSize : packoffset(c80);
	float4 SunsetColor : packoffset(c70);
	float4 TopCloudBiasDetailThresholdHeight : packoffset(c74);
	float4 TopCloudColor : packoffset(c75);
	float4 gtaSkyDomeFade : packoffset(c67);
	float4 gtaWaterColor : packoffset(c68);
};

cbuffer SharedConstants : register(b2, space4)
{
	uint GalaxySampler_Texture2DDescriptorIndex : packoffset(c0.y);
	uint GalaxySampler_Texture2DArrayDescriptorIndex : packoffset(c6.w);
	uint GalaxySampler_Texture3DDescriptorIndex : packoffset(c13.y);
	uint GalaxySampler_TextureCubeDescriptorIndex : packoffset(c19.w);
	uint GalaxySampler_SamplerDescriptorIndex : packoffset(c26.y);
	uint HighDetailNoiseSampler_Texture2DDescriptorIndex : packoffset(c0.w);
	uint HighDetailNoiseSampler_Texture2DArrayDescriptorIndex : packoffset(c7.y);
	uint HighDetailNoiseSampler_Texture3DDescriptorIndex : packoffset(c13.w);
	uint HighDetailNoiseSampler_TextureCubeDescriptorIndex : packoffset(c20.y);
	uint HighDetailNoiseSampler_SamplerDescriptorIndex : packoffset(c26.w);
	uint PerlinNoiseSampler_Texture2DDescriptorIndex : packoffset(c0.z);
	uint PerlinNoiseSampler_Texture2DArrayDescriptorIndex : packoffset(c7.x);
	uint PerlinNoiseSampler_Texture3DDescriptorIndex : packoffset(c13.z);
	uint PerlinNoiseSampler_TextureCubeDescriptorIndex : packoffset(c20.x);
	uint PerlinNoiseSampler_SamplerDescriptorIndex : packoffset(c26.z);
	uint StarFieldSampler_Texture2DDescriptorIndex : packoffset(c0.x);
	uint StarFieldSampler_Texture2DArrayDescriptorIndex : packoffset(c6.z);
	uint StarFieldSampler_Texture3DDescriptorIndex : packoffset(c13.x);
	uint StarFieldSampler_TextureCubeDescriptorIndex : packoffset(c19.z);
	uint StarFieldSampler_SamplerDescriptorIndex : packoffset(c26.x);
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
	float4 c251 = as_type<float4>(uint4(0x40400000, 0x3EB33333, 0x0, 0x0));
#else
	float4 c251 = asfloat(uint4(0x40400000, 0x3EB33333, 0x0, 0x0));
#endif
#ifdef __air__
	float4 c252 = as_type<float4>(uint4(0x3F000000, 0x3F700000, 0x3F19999A, 0x3F99999A));
#else
	float4 c252 = asfloat(uint4(0x3F000000, 0x3F700000, 0x3F19999A, 0x3F99999A));
#endif
#ifdef __air__
	float4 c253 = as_type<float4>(uint4(0x3E4CCCCD, 0x3F800000, 0x42000000, 0x0));
#else
	float4 c253 = asfloat(uint4(0x3E4CCCCD, 0x3F800000, 0x42000000, 0x0));
#endif
#ifdef __air__
	float4 c254 = as_type<float4>(uint4(0x40800000, 0x3E800000, 0xBD800000, 0x3F000000));
#else
	float4 c254 = asfloat(uint4(0x40800000, 0x3E800000, 0xBD800000, 0x3F000000));
#endif
#ifdef __air__
	float4 c255 = as_type<float4>(uint4(0x3E4CCCCD, 0xC139DCA9, 0x414947AE, 0xC0000000));
#else
	float4 c255 = asfloat(uint4(0x3E4CCCCD, 0xC139DCA9, 0x414947AE, 0xC0000000));
#endif

	float4 r0 = input.iTexCoord0;
	float4 r1 = input.iTexCoord1;
	float4 r2 = input.iTexCoord2;
	float4 r3 = input.iTexCoord3;
	float4 r4 = input.iTexCoord4;
	float4 r5 = input.iTexCoord5;
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

	r6.xy = (float2)((r2.xy * CloudThicknessEdgeSmoothDetailScaleStrength.zz));
	r8.w = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		PerlinNoiseSampler_Texture2DDescriptorIndex, PerlinNoiseSampler_SamplerDescriptorIndex, r4.xy, float2(0, 0)).y;
	r10.x = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		PerlinNoiseSampler_Texture2DDescriptorIndex, PerlinNoiseSampler_SamplerDescriptorIndex, r2.xy, float2(0, 0)).x;
	r10.yz = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		PerlinNoiseSampler_Texture2DDescriptorIndex, PerlinNoiseSampler_SamplerDescriptorIndex, r0.xy, float2(0, 0)).xz;
	r6.x = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		HighDetailNoiseSampler_Texture2DDescriptorIndex, HighDetailNoiseSampler_SamplerDescriptorIndex, r6.xy, float2(0, 0)).x;
	r0.y = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		HighDetailNoiseSampler_Texture2DDescriptorIndex, HighDetailNoiseSampler_SamplerDescriptorIndex, r2.zw, float2(0, 0)).x;
	r8.xyz = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		GalaxySampler_Texture2DDescriptorIndex, GalaxySampler_SamplerDescriptorIndex, r3.zw, float2(0, 0)).xyz;
	r2.yzw = tfetch2D(
#ifdef __air__
		g_Texture2DDescriptorHeap,
		g_SamplerDescriptorHeap,
#endif
		StarFieldSampler_Texture2DDescriptorIndex, StarFieldSampler_SamplerDescriptorIndex, r3.xy, float2(0, 0)).xyz;
	r3.w = (float)((StarFieldBrightness.y * c252.w));
	ps = max(SunDirection.w, SunDirection.w);
	r0.x = (float)((dot(r1.zxy, r1.zxy)));
	ps = saturate((float)(c252.z + ps));
	r2.x = ps;
	r9.xyz = (float3)((r2.yzw * StarFieldBrightness.xxx + -StarFieldBrightness.zzz));
	r7.xy = (float2)((selectWrapper(c253.wy == 0.0, r8.xx, c252.ww)));
	r6.y = (float)((r0.y + -c254.w));
	ps = clamp(rsqrt(abs(r0.x)), -FLT_MAX, FLT_MAX);
	r0.x = ps;
	r12.xyz = (float3)((r0.xxx * r1.xyz));
	ps = max(-HDRSunExposure.y, -HDRSunExposure.y);
	r12.w = (float)((dot(r12.zxy, SunDirection.zxy)));
	ps = saturate((float)(c253.y + ps));
	r6.z = ps;
	r0.xyzw = (float4)((r12.wyww * c254.yxzw));
	ps = r12.w * r12.w;
	r2.z = ps;
	r0.zw = (float2)((saturate(r0.wz + c252.xy)));
	ps = max(r0.y, r0.y);
	r0.y = ps;
	r1.x = (float)((r0.w * r0.w));
	ps = max(r0.x, r0.x);
	r0.x = ps;
	r6.w = (float)((saturate(r0.z * SunCentre.y + SunCentre.x)));
	r2.y = (float)((r1.x * r0.w));
	ps = c251.y + r0.x;
	r7.z = ps;
	r3.xyz = (float3)((r8.yzx * StarFieldBrightness.yyy));
	ps = r6.w * r6.w;
	r9.w = ps;
	r7.x = (float)((dot(r3.yxw, r7.yyx)));
	ps = max(r6.y, r6.y);
	r0.x = ps;
	r11.xyzw = (float4)((r6.yzzw * c255.xyzw));
	ps = CloudThicknessEdgeSmoothDetailScaleStrength.w * r0.x;
	r1.x = ps;
	r0.x = (float)((saturate(r11.z * r12.w + r11.y)));
	r7.y = (float)((r11.w + c251.x));
	ps = CloudThicknessEdgeSmoothDetailScaleStrength.w * r6.x;
	r1.z = ps;
	r6.xyzw = (float4)((r9.xwyz * r7.xyxx));
	ps = clamp(log2(r0.x), -FLT_MAX, FLT_MAX);
	r1.w = ps;
	r12.xyw = (float3)((r1.zxw * c253.xyz));
	ps = SunSize.x * r0.z;
	r10.w = ps;
	r7.y = (float)((r6.y * SunCentre.z));
	ps = exp2(r12.w);
	r0.x = ps;
	r12.z = (float)((-r2.y * r2.x + r0.x));
	r2.xyw = (float3)((r12.zxy + r10.wxy));
	ps = max(r11.x, r11.x);
	r0.xz = (float2)((r2.wy * CloudThreshold.xx));
	ps = saturate((float)(r10.z + ps));
	r6.y = ps;
	r4.w = (float)((saturate(r0.x + -CloudBias.x)));
	ps = saturate((float)(c254.w * r2.x));
	r0.x = ps;
	r2.x = (float)((saturate(r4.w * CloudThicknessEdgeSmoothDetailScaleStrength.y)));
	ps = -CloudBias.x - -r0.z;
	r0.z = ps;
	r0.w = (float)((saturate(r0.z * CloudShadowStrength.x)));
	ps = clamp(log2(r4.w), -FLT_MAX, FLT_MAX);
	r0.z = ps;
	r8.xyz = (float3)((r9.xyz + r6.xzw));
	ps = CloudThicknessEdgeSmoothDetailScaleStrength.x * r0.z;
	r0.z = ps;
	r6.zw = (float2)((r0.wx * c254.wx));
	ps = exp2(r0.z);
	r0.x = ps;
	r0.x = (float)((r2.x * r0.x));
	ps = c253.y - r0.w;
	r0.z = ps;
	r0.z = (float)((r0.x * r0.z));
	ps = c253.y - r0.x;
	r6.x = ps;
	r7.x = (float)((r0.z * r6.y));
	ps = max(r6.x, r6.x);
	r2.xy = (float2)((r6.wy + r7.yx));
	ps = saturate((float)(-r6.z + ps));
	r7.w = ps;
	r6.yzw = (float3)((r2.xxx * SunColor.xyz));
	ps = saturate((float)(c253.y - r2.x));
	r7.x = ps;
	r3.xyz = (float3)((saturate(r8.xyz + r3.zxy)));
	ps = abs(r6.y) * abs(r6.y);
	r7.y = ps;
	r2.xy = (float2)((r2.yz * r7.zw));
	ps = abs(r6.z) * abs(r6.z);
	r7.z = ps;
	r8.xyz = (float3)((r2.xxx * SunsetColor.xyz));
	ps = abs(r6.w) * abs(r6.w);
	r7.w = ps;
	r7.yzw = (float3)((r7.yzw * r7.yzw + r6.yzw));
	r0.z = (float)((r2.y * CloudInscatteringRange.x + c253.y));
	r2.xyz = (float3)((r0.zzz * CloudColor.xyz + -r0.www));
	r7.xyzw = (float4)((r7.xyzw * HDRSunExposure.xxxx));
	ps = TopCloudBiasDetailThresholdHeight.y * r1.x;
	r2.w = ps;
	r2.xyzw = (float4)((r2.yzxw + r8.yzxw));
	ps = saturate((float)(-CloudFadeOut.x - -r0.y));
	r0.w = ps;
	r5.xyz = (float3)((saturate(r7.xxx * r5.xyz)));
	ps = max(r2.z, r2.z);
	r0.y = ps;
	r5.xyz = (float3)((r5.xyz + r7.yzw));
	ps = TopCloudColor.x - r0.y;
	r6.y = ps;
	r0.y = (float)((saturate(r2.w * TopCloudBiasDetailThresholdHeight.z + -TopCloudBiasDetailThresholdHeight.x)));
	r0.y = (float)((r0.y * r4.z));
	ps = TopCloudColor.y - r2.x;
	r6.z = ps;
	r0.y = (float)((r0.y * r6.x));
	ps = TopCloudColor.z - r2.y;
	r6.w = ps;
	r2.xyz = (float3)((r0.yyy * r6.yzw + r2.zxy));
	r1.w = (float)((r0.y * r6.x + r0.x));
	r0.xyz = (float3)((r5.xyz + r3.xyz));
	ps = max(r1.w, r1.w);
	r2.xyz = (float3)((r2.xyz + -r0.xyz));
	ps = r0.w * ps;
	r0.w = ps;
  const uint fog_valid = vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 0x2D0);
  const bool reflection_fog = (fog_valid & 0xF0u) == 0xF0u &&
      vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 0x42C) != 0;
  if (reflection_fog) {
    const float4 fog = vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + 0x260, 16);
    float slope = input.iTexCoord6.y * fog.y;
    if (abs(slope) < 1.0e-6) slope = 1.0e-6;
    float density = exp2(input.iTexCoord6.x * fog.y * fog.z) *
        (1.0 - exp2(-slope * input.iTexCoord6.z)) / slope * fog.x;
    r0.w *= 1.0 - pow(saturate(1.0 - exp2(-density)), fog.w);
  }
	r0.xyz = (float3)((r0.www * r2.xyz + r0.xyz));
	// Publish the cloud blend transmittance, before exposure and water fade.
	const uint64_t cloud_address = vk::RawBufferLoad<uint64_t>(g_PushConstants.SharedConstants + 0x490, 8);
	const uint2 cloud_extent = vk::RawBufferLoad<uint2>(g_PushConstants.SharedConstants + 0x498, 8);
	const uint2 cloud_pixel = uint2(input.iPos.xy);
	if (cloud_address != 0 && all(cloud_pixel < cloud_extent))
	  vk::RawBufferStore<float>(cloud_address + uint64_t(cloud_pixel.y * cloud_extent.x + cloud_pixel.x) * 4u, saturate(1.0 - r0.w));
	r0.xyz = (float3)((r0.xyz * HDRExposure.xxx));
	ps = max(-r1.y, -r1.y);
  r0.yzw = (fog_valid & 0xF0u) == 0xF0u ? r0.xyz : min(r0.xyz, HDRExposureClamp.xyz);
  if (reflection_fog) {
    const float4 near_color = vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 0x2B0, 16);
    const float4 fog_parameters = vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 0x290, 16);
    r0.yzw = lerp(r0.yzw, near_color.xyz, fog_parameters.w);
  }
	ps = gtaSkyDomeFade.x + ps;
	r0.x = ps;
	r1.xyz = (float3)((-r0.yzw + gtaWaterColor.xyz));
	ps = saturate((float)(gtaSkyDomeFade.y * r0.x));
	r0.x = ps;
	output.oC0.xyz = (float3)((r0.xxxx * r1.xyzz + r0.yzww).xyz);
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
	return output;
}