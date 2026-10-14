// Stage 2 is replaced only after the complete host DoF chain succeeds.
#if defined(__spirv__)
#define g_AlphaToMask vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 736)
#define g_AlphaToMaskSampleCount vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 740)
#define LibertySplitPostFxApplied vk::RawBufferLoad<float>(g_PushConstants.SharedConstants + 596)
#elif defined(__air__)
#define g_AlphaToMask (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 736)))
#define g_AlphaToMaskSampleCount (*(reinterpret_cast<device uint*>(g_PushConstants.SharedConstants + 740)))
#define LibertySplitPostFxApplied (*(reinterpret_cast<device float*>(g_PushConstants.SharedConstants + 596)))
#else
#define LibertySplitPostFxApplied 0.0f
#endif
