#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <rex/logging.h>
#include <rex/memory.h>
#include "graphics/gta4_metal/resources.h"
#include "ui/metal/context.h"

static void Check(bool value,const std::string& message) {if(!value) throw std::runtime_error(message);}

#include "metal_texture_channels.inc"
#include "metal_texture_endian_cases.inc"
#include "metal_sampler_features.inc"
#include "metal_buffer_pressure.inc"
#include "metal_texture_pressure.inc"
#include "metal_font_cases.inc"
#include "metal_font_corpus_cases.inc"
#include "metal_texture_preparation_cases.inc"
#include "metal_vertex_conversion_cache_cases.inc"
#include "metal_guest_access_cases.inc"

int main(int argc, char** argv) {
  @autoreleasepool {
    try {
      using namespace rex;
      using namespace rex::graphics;
      using namespace rex::graphics::gta4_metal;
      InitLogging();
      ExerciseTextureEndianBounds();
      memory::Memory memory; Check(memory.Initialize(),"Guest memory reservation failed");
      ExerciseGuestAccess(memory);
      BenchmarkGuestAccess(memory);
      auto* virtual_heap=memory.LookupHeap(0x10000000);
      auto* physical_heap=memory.GetPhysicalHeap();
      Check(virtual_heap && physical_heap,"Missing guest heaps");
      uint32_t virtual_address=0,physical_address=0;
      const uint32_t allocation=memory::kMemoryAllocationReserve|memory::kMemoryAllocationCommit;
      const uint32_t protection=memory::kMemoryProtectRead|memory::kMemoryProtectWrite;
      Check(virtual_heap->Alloc(4096,4096,allocation,protection,false,&virtual_address),"Guest header allocation failed");
      Check(physical_heap->Alloc(4096,4096,allocation,protection,false,&physical_address),"Guest texture allocation failed");
      GuestMemory guest(&memory);
      Check(guest.Read(virtual_address,4096).size()==4096,"Allocated guest span unreadable");
      Check(guest.Read(UINT32_MAX,16).empty(),"Wrapping guest span accepted");
      Check(guest.Read(0x20000000,16,true).empty(),"Out-of-range physical span accepted");
      std::string error;
      auto context=ui::metal::MetalContext::Create(error); Check(bool(context),error);
      ResourceStore resources(context,guest); resources.BeginFrame();
      ExerciseTexturePreparation(context, memory);
      ExerciseVertexConversionRetention(context, memory);
      ExerciseTextureChannels(context, resources);
      ExerciseBufferPressure(context, memory);
      ExerciseTexturePressure(context, memory);
      ExerciseSamplerFeatures(context, memory);
      ExerciseFontAtlases(context, memory);
      Check(argc <= 2, "usage: rex-metal-resources-test [font-corpus-directory]");
      if (argc == 2) ExerciseFontCorpus(context, memory, argv[1]);
      auto store_word=[&](uint32_t address,uint32_t value) {
        value=__builtin_bswap32(value);
        Check(guest.Write(address,{reinterpret_cast<const uint8_t*>(&value),sizeof(value)}),"Guest word write failed");
      };
      const uint32_t vb=virtual_address,payload=virtual_address+512;
      store_word(vb,1); store_word(vb+24,payload); store_word(vb+28,32|2);
      for(uint32_t i=0;i<8;++i) store_word(payload+i*4,std::bit_cast<uint32_t>(float(i)));
      VertexDeclaration declaration; declaration.identity=1;
      gta4_native::VertexElement element{}; element.stream=0; element.offset=0; element.type=0x1A23A6; element.usage=0; element.usage_index=0;
      declaration.elements.push_back(element);
      ShaderMetadata shader{}; shader.hash=1; shader.attribute_count=1;
      shader.attributes[0]={0,0,MetalVertexScalar::kFloat,4};
      auto first=resources.VertexBuffer(vb,declaration,shader,0,0,16,error); Check(first!=nil,error);
      Check(static_cast<const float*>(first.contents)[7]==7.0f,"Vertex endian conversion failed");
      auto reused=resources.VertexBuffer(vb,declaration,shader,0,0,16,error); Check(first==reused,"Clean vertex conversion was not reused");
      Check(first.cpuCacheMode == MTLCPUCacheModeDefaultCache, "CPU-readable vertex storage is not cached");
      // More shader/declaration identities than the conversion cache capacity
      // must still reuse a single immutable endian-only buffer.
      for (uint64_t identity = 2; identity < 12; ++identity) {
        auto same_declaration = declaration; same_declaration.identity = identity;
        auto same_shader = shader; same_shader.hash = identity;
        Check(resources.VertexBuffer(vb, same_declaration, same_shader, 0, 0, 16, error) == first,
              "Identical vertex byte conversions were duplicated across shaders");
      }
      auto half_declaration = declaration; half_declaration.identity = 99;
      half_declaration.elements[0].type = 0x1A235A;
      auto half = resources.VertexBuffer(vb, half_declaration, shader, 0, 0, 16, error);
      Check(half != nil && half != first, "Halfword correction incorrectly reused word-only bytes");
      const auto original_bytes = guest.Read(payload, 32);
      for (size_t byte = 0; byte < original_bytes.size(); byte += 16) {
        for (size_t component = 0; component < 4; ++component) {
          uint16_t original, actual;
          std::memcpy(&original, original_bytes.data() + byte + component * sizeof(uint16_t), sizeof(original));
          std::memcpy(&actual, static_cast<const uint8_t*>(half.contents) + byte + component * sizeof(uint16_t), sizeof(actual));
          Check(actual == __builtin_bswap16(original), "Halfword correction changed its numeric value");
        }
      }
      Check(resources.VertexBuffer(vb, declaration, shader, 0, 0, 16, error) == first,
            "Attribute correction displaced the compatible endian-only generation");
      store_word(payload,std::bit_cast<uint32_t>(99.0f)); resources.Invalidate(vb);
      auto changed=resources.VertexBuffer(vb,declaration,shader,0,0,16,error); Check(changed!=nil && changed!=first,error);
      Check(static_cast<const float*>(first.contents)[0]==0 && static_cast<const float*>(changed.contents)[0]==99,"In-flight vertex generation was mutated");
      const uint32_t ib=virtual_address+64,index_data=virtual_address+1024;
      store_word(ib,2); store_word(ib+24,index_data); store_word(ib+28,6);
      std::array<uint16_t,3> indices{__builtin_bswap16(2),__builtin_bswap16(1),0};
      Check(guest.Write(index_data,{reinterpret_cast<const uint8_t*>(indices.data()),sizeof(indices)}),"Index payload write failed");
      bool index32=false; auto index=resources.IndexBuffer(ib,index32,error); Check(index!=nil && !index32,error);
      Check(static_cast<const uint16_t*>(index.contents)[0]==2,"Index endian conversion failed");
      Check(index.cpuCacheMode == MTLCPUCacheModeDefaultCache, "CPU-readable index storage is not cached");
      std::array<uint8_t,4096> texels{};
      for(size_t y=0;y<8;++y) for(size_t x=0;x<16;++x) {
        const size_t offset=y*256+x*4;
        texels[offset]=uint8_t(x*8); texels[offset+1]=uint8_t(y*16); texels[offset+2]=64; texels[offset+3]=255;
      }
      Check(guest.Write(physical_address,texels,true),"Guest physical texture write failed");
      xenos::xe_gpu_texture_fetch_t fetch{};
      fetch.type=xenos::FetchConstantType::kTexture;
      fetch.format=xenos::TextureFormat::k_8_8_8_8;
      fetch.dimension=xenos::DataDimension::k2DOrStacked;
      fetch.base_address=physical_address>>12;
      fetch.pitch=2; fetch.size_2d.width=15; fetch.size_2d.height=7;
      fetch.swizzle=xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA;
      auto commands=[context->queue commandBuffer];
      bool ended=false;
      auto texture=resources.Texture(virtual_address+128,fetch,commands,[&]{ended=true;},error);
      Check(bool(texture) && ended,error);
      Check(texture->image.width==16 && texture->image.height==8,"Texture dimensions changed");
      auto view=resources.View(texture,fetch,error); Check(view!=nil,error);
      Check(resources.View(texture,fetch,error)==view,"Texture view was not cached");
      auto sampler=resources.Sampler(fetch,error,texture.get()); Check(sampler!=nil,error);
      Check(resources.Sampler(fetch,error,texture.get())==sampler,"Sampler was not cached");
      auto readback=[context->device newBufferWithLength:4096 options:MTLResourceStorageModeShared];
      auto blit=[commands blitCommandEncoder];
      [blit copyFromTexture:texture->image sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
          sourceSize:MTLSizeMake(16,8,1) toBuffer:readback destinationOffset:0 destinationBytesPerRow:256 destinationBytesPerImage:2048];
      [blit endEncoding];
      // Release the guest name before submission. Encoded GPU references must
      // retain the actual image, even after every CPU cache owner is removed.
      resources.Release(virtual_address+128); texture.reset(); view=nil;
      [commands commit]; [commands waitUntilCompleted];
      Check(commands.status==MTLCommandBufferStatusCompleted,ui::metal::MetalError(commands.error,"Texture GPU execution failed"));
      for(size_t y=0;y<8;++y) Check(!std::memcmp(static_cast<const uint8_t*>(readback.contents)+y*256,texels.data()+y*256,64),"Texture upload pixel mismatch");
      gta4_native::SurfaceDescriptor descriptor{};
      descriptor.handle=0x1234; descriptor.width=16; descriptor.height=16; descriptor.format=0x18280186;
      for(uint32_t sample_type=0;sample_type<3;++sample_type) {
        descriptor.sample_type=sample_type;
        auto color=resources.Surface(descriptor,false,error); Check(bool(color),error);
        Check(color->image.sampleCount==(1u<<sample_type),"Surface sample count changed");
        // The existing allocation may be reused only after the full descriptor
        // comparison. New unsupported samples must still reject, not reuse it.
        auto comparable=descriptor;comparable.base=16u|(sample_type<<16);
        color=resources.Surface(comparable,false,error);Check(bool(color),error);
        Check(resources.Surface(comparable,false,error)==color,"Matching supported surface was not reused");
        if(![context->device supportsTextureSampleCount:3]){
          Check(!resources.Surface(comparable,false,error,3),"Unsupported new samples reused an old allocation");
          Check(resources.Surface(comparable,false,error)==color,"Rejected sample request invalidated a valid surface");
        }
        auto changed_extent=comparable;changed_extent.width=8;
        const auto smaller=resources.Surface(changed_extent,false,error);Check(bool(smaller),error);
        Check(smaller!=color&&smaller->image.width==8,"Surface extent change reused stale storage");
        color=resources.Surface(comparable,false,error);Check(bool(color),error);

        auto depth_desc=descriptor; depth_desc.handle=0x1238; depth_desc.format=0x1A220197;
        auto depth=resources.Surface(depth_desc,true,error); Check(bool(depth),error);
        auto pass=[MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture=color->image; pass.colorAttachments[0].loadAction=MTLLoadActionClear; pass.colorAttachments[0].storeAction=MTLStoreActionStore;
        pass.depthAttachment.texture=depth->image; pass.depthAttachment.loadAction=MTLLoadActionClear; pass.depthAttachment.storeAction=MTLStoreActionStore;
        pass.stencilAttachment.texture=depth->image; pass.stencilAttachment.loadAction=MTLLoadActionClear; pass.stencilAttachment.storeAction=MTLStoreActionStore;
        auto clear=[context->queue commandBuffer]; auto encoder=[clear renderCommandEncoderWithDescriptor:pass]; [encoder endEncoding];
        [clear commit]; [clear waitUntilCompleted]; Check(clear.status==MTLCommandBufferStatusCompleted,"Surface clear failed");
      }
      // Surface and sampled-texture names form one reflection registration.
      // Releasing either name must retire classification of both names.
      for (bool release_texture : {false, true}) {
        gta4_native::RegisterReflectionTargetCommand reflection{};
        reflection.surface = 0x4100; reflection.texture = 0x4200; reflection.wrapper = 0x4300;
        reflection.logical_width = reflection.logical_height = 16;
        reflection.physical_width = reflection.physical_height = 32;
        resources.RegisterReflection(reflection);
        Check(resources.IsReflection(reflection.surface) && resources.IsReflection(reflection.texture),
              "Reflection pair registration is incomplete");
        resources.Release(release_texture ? reflection.texture : reflection.surface);
        Check(!resources.IsReflection(reflection.surface) && !resources.IsReflection(reflection.texture),
              "Released reflection left a stale companion classification");
        resources.RegisterReflection(reflection);
        const uint32_t old_texture = reflection.texture;
        reflection.texture = 0x4400; reflection.wrapper = 0x4500;
        resources.RegisterReflection(reflection);
        Check(!resources.IsReflection(old_texture) && resources.IsReflection(reflection.surface) &&
              resources.IsReflection(reflection.texture), "Reflection replacement retained its previous pair");
        resources.Release(reflection.surface);
      }
      // Initializing unsampled mips must not erase already produced contents
      // or label a deterministic clear as a completed guest resolve.
      gta4_native::ResolveCommand allocation_request{};
      allocation_request.destination_texture = virtual_address + 256;
      uint32_t mip_backing = 0;
      Check(physical_heap->Alloc(4096, 4096, allocation, protection, false, &mip_backing),
            "Mip backing allocation failed");
      auto mip_fetch = fetch; mip_fetch.mip_max_level = 1; mip_fetch.mip_address = mip_backing >> 12;
      std::memcpy(allocation_request.destination_fetch, &mip_fetch, sizeof(mip_fetch));
      auto mip_texture = resources.ResolveTarget(allocation_request, error); Check(bool(mip_texture), error);
      Check(mip_texture->image.mipmapLevelCount == 2, "Mip fixture lost its second level");
      auto initialization = [context->queue commandBuffer];
      auto first_pass = [MTLRenderPassDescriptor renderPassDescriptor];
      first_pass.colorAttachments[0].texture = mip_texture->image;
      first_pass.colorAttachments[0].loadAction = MTLLoadActionClear;
      first_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
      first_pass.colorAttachments[0].clearColor = MTLClearColorMake(1, 0, 0, 1);
      auto first_encoder = [initialization renderCommandEncoderWithDescriptor:first_pass];
      Check(first_encoder != nil, "Produced mip encoder unavailable"); [first_encoder endEncoding];
      mip_texture->subresource_writes[0] = 1; mip_texture->content_serial = 1; mip_texture->initialized = true;
      size_t end_calls = 0;
      Check(resources.InitializeTextureStorage(mip_texture, initialization, [&] { ++end_calls; }, error), error);
      Check(resources.InitializeTextureStorage(mip_texture, initialization, [&] { ++end_calls; }, error), error);
      Check(end_calls == 1 && mip_texture->storage_initialized && mip_texture->initialized_subresources.size() == 1,
            "Complete storage initialization was repeated");
      Check(mip_texture->subresource_writes.size() == 1 && mip_texture->content_serial == 1 &&
            mip_texture->initialized_subresources.front() == (uint64_t{1} << 32),
            "Initialization incorrectly recorded a title-produced mip");
      auto cached = resources.Texture(allocation_request.destination_texture, mip_fetch, initialization,
          [&] { ++end_calls; }, error);
      Check(cached == mip_texture && end_calls == 1, "Sampling repeated completed texture initialization");
      const size_t row_pitch = 256;
      const size_t base_bytes = row_pitch * mip_texture->image.height;
      const size_t mip_bytes = row_pitch * (mip_texture->image.height >> 1);
      auto mip_readback = [context->device newBufferWithLength:base_bytes + mip_bytes options:MTLResourceStorageModeShared];
      Check(mip_readback != nil, "Mip readback allocation failed");
      auto mip_blit = [initialization blitCommandEncoder];
      Check(mip_blit != nil, "Mip readback encoder unavailable");
      for (NSUInteger level = 0; level < 2; ++level) {
        [mip_blit copyFromTexture:mip_texture->image sourceSlice:0 sourceLevel:level
            sourceOrigin:MTLOriginMake(0, 0, 0)
            sourceSize:MTLSizeMake(mip_texture->image.width >> level, mip_texture->image.height >> level, 1)
            toBuffer:mip_readback destinationOffset:level ? base_bytes : 0
            destinationBytesPerRow:row_pitch destinationBytesPerImage:level ? mip_bytes : base_bytes];
      }
      [mip_blit endEncoding]; [initialization commit]; [initialization waitUntilCompleted];
      Check(initialization.status == MTLCommandBufferStatusCompleted,
            ui::metal::MetalError(initialization.error, "Mip initialization GPU failure"));
      for (size_t level = 0; level < 2; ++level)
        for (size_t y = 0; y < (mip_texture->image.height >> level); ++y)
          for (size_t x = 0; x < (mip_texture->image.width >> level); ++x) {
            uint32_t pixel = 0;
            std::memcpy(&pixel, static_cast<const uint8_t*>(mip_readback.contents) +
                (level ? base_bytes : 0) + y * row_pitch + x * sizeof(pixel), sizeof(pixel));
            Check(pixel == (level ? 0u : 0xFF0000FFu), "Initialization erased a produced mip or left undefined texels");
          }
      std::printf("reflection_lifetime_and_mip_initialization=passed\n");
      resources.Clear(); Check(resources.texture_bytes()==0 && resources.buffer_bytes()==0,"Resource cache cleanup retained entries");
      virtual_heap->Release(virtual_address); physical_heap->Release(physical_address);
      std::printf("metal_resources_gpu=passed device=%s\n",context->device.name.UTF8String);
      return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"Metal resource test: %s\n",error.what()); return 1;}
  }
}
