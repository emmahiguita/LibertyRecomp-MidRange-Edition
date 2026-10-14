#include "resource_state.h"
#include "../gta4_native/texture_filtering_policy.h"
#include "../gta4_native/anisotropic_filtering_policy.h"
#include <rex/cvar.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <array>
#include <bit>
#include <cstring>

namespace rex::graphics::gta4_metal {
namespace {
MTLSamplerAddressMode Address(xenos::ClampMode mode) {
  switch(mode) {
    case xenos::ClampMode::kRepeat: return MTLSamplerAddressModeRepeat;
    case xenos::ClampMode::kMirroredRepeat: return MTLSamplerAddressModeMirrorRepeat;
    case xenos::ClampMode::kClampToBorder: return MTLSamplerAddressModeClampToBorderColor;
    case xenos::ClampMode::kMirrorClampToEdge:
    case xenos::ClampMode::kMirrorClampToHalfway:
    case xenos::ClampMode::kMirrorClampToBorder: return MTLSamplerAddressModeMirrorClampToEdge;
    default: return MTLSamplerAddressModeClampToEdge;
  }
}
}
void ResourceStore::BeginFrame() {
  state_->preparation.BeginFrame(); // Revalidate retained immutable jobs at their next consumption.
  const auto fonts=rex::cvar::Query<bool>("gta4_native_vector_fonts");
  const auto filtering=rex::cvar::GetFlagByName("gta4_texture_filtering");
  const auto anisotropy=rex::cvar::GetFlagByName("gta4_anisotropic_filtering");
  if(fonts!=state_->vector_fonts||filtering!=state_->filtering||anisotropy!=state_->anisotropy)state_->InvalidateBindings();
  state_->vector_fonts = rex::cvar::Query<bool>("gta4_native_vector_fonts");
  state_->filtering=rex::cvar::GetFlagByName("gta4_texture_filtering");
  state_->anisotropy=rex::cvar::GetFlagByName("gta4_anisotropic_filtering");
}

id<MTLSamplerState> ResourceStore::Sampler(const xenos::xe_gpu_texture_fetch_t& fetch,
    std::string& error,const TextureResource* texture) {
  @autoreleasepool {
    error.clear();
    xenos::ClampMode u,v,w; texture_util::GetClampModesForDimension(fetch,u,v,w);
    auto normalize=[](xenos::TextureFilter filter) {return filter==xenos::TextureFilter::kUseFetchConst ? xenos::TextureFilter::kPoint : filter;};
    gta4_native::MaterialAnisotropicFilterState state{normalize(fetch.min_filter),normalize(fetch.mag_filter),
        normalize(fetch.mip_filter),fetch.aniso_filter==xenos::AnisoFilter::kUseFetchConst ? xenos::AnisoFilter::kDisabled : fetch.aniso_filter};
    uint32_t minimum=0,maximum=0;
    texture_util::GetSubresourcesFromFetchConstant(fetch,nullptr,nullptr,nullptr,nullptr,nullptr,&minimum,&maximum);
    if(state.mip_filter==xenos::TextureFilter::kBaseMap) maximum=minimum;
    if(texture) {
      minimum=std::min(minimum,uint32_t(texture->image.mipmapLevelCount-1));
      maximum=std::clamp(maximum,minimum,uint32_t(texture->image.mipmapLevelCount-1));
    }
    const bool material=texture && !texture->gpu_produced && texture->image.mipmapLevelCount>1 &&
        state.mip_filter!=xenos::TextureFilter::kBaseMap;
    const auto filtered=gta4_native::ApplyMaterialTextureFiltering({state.min_filter,state.mag_filter,state.mip_filter},state_->filtering,material);
    state.min_filter=filtered.min_filter; state.mag_filter=filtered.mag_filter; state.mip_filter=filtered.mip_filter;
    state=gta4_native::ApplyMaterialAnisotropicFiltering(state,state_->anisotropy,material);
    if (texture && texture->font_replacement) {
      state.min_filter = state.mag_filter = state.mip_filter = xenos::TextureFilter::kLinear;
      state.aniso_filter = xenos::AnisoFilter::kDisabled;
      minimum = 0; maximum = uint32_t(texture->image.mipmapLevelCount - 1);
    }
    const bool border=xenos::ClampModeUsesBorder(u)||xenos::ClampModeUsesBorder(v)||xenos::ClampModeUsesBorder(w);
    const uint32_t white=border && fetch.border_color==xenos::BorderColor::k_ABGR_White;
    const uint32_t anisotropy=state.aniso_filter==xenos::AnisoFilter::kDisabled ? 1 :
        1u<<std::min(4u,uint32_t(state.aniso_filter)-1u);
    const std::array<uint32_t,10> words{uint32_t(Address(u)),uint32_t(Address(v)),uint32_t(Address(w)),
        uint32_t(state.min_filter),uint32_t(state.mag_filter),uint32_t(state.mip_filter),minimum,maximum,white,anisotropy};
    const SamplerKey key{words};
    if(const auto found=state_->samplers.find(key);found!=state_->samplers.end()) return found->second;
    // Retain the finite working set for this session. Samplers referenced only
    // through argument buffers are not inferred from a buffer's raw bytes.
    if(state_->samplers.size()>=4096) {error="Metal sampler working set exceeds its bound"; return nil;}
    auto descriptor=[MTLSamplerDescriptor new];
    descriptor.supportArgumentBuffers=YES;
    descriptor.sAddressMode=Address(u); descriptor.tAddressMode=Address(v); descriptor.rAddressMode=Address(w);
    descriptor.minFilter=state.min_filter==xenos::TextureFilter::kLinear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    descriptor.magFilter=state.mag_filter==xenos::TextureFilter::kLinear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    descriptor.mipFilter=state.mip_filter==xenos::TextureFilter::kLinear ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNearest;
    descriptor.lodMinClamp=minimum; descriptor.lodMaxClamp=maximum;
    descriptor.maxAnisotropy=anisotropy;
    descriptor.borderColor=white ? MTLSamplerBorderColorOpaqueWhite : MTLSamplerBorderColorTransparentBlack;
    auto sampler=[state_->context->device newSamplerStateWithDescriptor:descriptor];
    if(!sampler) {error="Metal sampler allocation failed"; return nil;}
    state_->samplers.emplace(key,sampler); return sampler;
  }
}

const MaterialBinding* ResourceStore::FindMaterialBinding(uint32_t handle,const xenos::xe_gpu_texture_fetch_t& fetch){
  const auto words=std::bit_cast<std::array<uint32_t,6>>(fetch);
  // Four adjacent candidates tolerate ordinary handle alignment without an
  // allocation or a full material/sampler decode on a recurring asset bind.
  const size_t home=(handle>>4)%state_->material_bindings.size();
  for(size_t i=0;i<4;++i){
    auto& b=state_->material_bindings[(home+i)%state_->material_bindings.size()];
    if(b.epoch==state_->binding_epoch&&b.handle==handle&&b.fetch==words){
      state_->TouchTexture(*b.resource);b.resource->use_serial=++state_->serial;
      ++state_->material_hits;return &b;
    }
  }
  ++state_->material_misses;return nullptr;
}
const MaterialBinding* ResourceStore::RememberMaterialBinding(uint32_t handle,const xenos::xe_gpu_texture_fetch_t& fetch,
    const std::shared_ptr<TextureResource>& resource,id<MTLTexture> image,id<MTLSamplerState> sampler){
  if(!resource||resource->gpu_produced||state_->virtuals.Find(handle)||state_->reflections.contains(handle)||!image||!sampler)return nullptr;
  const auto found=state_->textures.find(handle);
  if(found==state_->textures.end()||found->second!=resource||state_->dirty.contains(handle))return nullptr;
  const size_t home=(handle>>4)%state_->material_bindings.size();size_t index=home;
  for(size_t i=0;i<4;++i){const size_t j=(home+i)%state_->material_bindings.size();
    if(state_->material_bindings[j].epoch!=state_->binding_epoch||state_->material_bindings[j].handle==handle){index=j;break;}}
  auto& b=state_->material_bindings[index];b.epoch=state_->binding_epoch;b.handle=handle;
  b.fetch=std::bit_cast<std::array<uint32_t,6>>(fetch);b.resource=resource.get();b.image=image;b.sampler=sampler;
  b.image_id=image.gpuResourceID._impl;b.sampler_id=sampler.gpuResourceID._impl;
  b.heap=image.textureType==MTLTextureTypeCube?3:image.textureType==MTLTextureType3D?2:image.textureType==MTLTextureType2DArray?1:0;
  b.mip_levels=uint32_t(image.mipmapLevelCount);return &b;
}
ResourceStore::MaterialStatistics ResourceStore::material_statistics()const{return {state_->material_hits,state_->material_misses};}
}  // namespace rex::graphics::gta4_metal
