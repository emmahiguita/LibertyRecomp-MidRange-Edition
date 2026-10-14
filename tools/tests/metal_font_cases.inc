#include "graphics/gta4_native/core/font_atlas.h"
#include <rex/cvar.h>
#include <xxhash.h>

static void ExerciseFontAtlases(const std::shared_ptr<rex::ui::metal::MetalContext>& context,
                               rex::memory::Memory& memory) {
  using namespace rex;
  using namespace rex::graphics;
  using namespace gta4_metal;
  namespace font = gta4_native::core;
  // Validate averaging separately from the shared reference used by GPU checks.
  const std::array<uint8_t, 16> pattern{0, 255, 0, 255, 255, 0, 255, 0,
                                       0, 255, 0, 255, 255, 0, 255, 0};
  const auto tiny = font::FontCoverageMips(pattern, 4, 3);
  Check(tiny.size() == 3 && tiny[1] == std::vector<uint8_t>(4, 128) && tiny[2][0] == 128,
        "Coverage mips do not preserve mean opacity");
  Check(font::FontCoverageMips(pattern, 3, 2).empty(), "Non-power-of-two font extent accepted");
  Check(font::FontCoverageMips(pattern, 4, 4).empty(), "Too many font mip levels accepted");
  Check(font::SelectVectorFontSet(1, 0xD319ABCEFD23508Dull) == font::VectorFontSet::kTbogt, "TBoGT font2 identity lost");
  Check(font::SelectVectorFontSet(2, 0x57551F2730323DF3ull) == font::VectorFontSet::kTlad, "TLAD font3 identity lost");
  Check(font::SelectVectorFontSet(2, 0x6C569517F27F53D2ull) == font::VectorFontSet::kTbogt, "TBoGT font3 identity lost");
  Check(font::SelectVectorFontSet(0, 0x6C569517F27F53D2ull) == font::VectorFontSet::kGta4, "Shared font1 was remapped by another atlas identity");
  for (auto set : {font::VectorFontSet::kGta4, font::VectorFontSet::kTlad, font::VectorFontSet::kTbogt})
    for (size_t index = 0; index < 3; ++index) {
      const auto* atlas = font::FindVectorFontAtlas(set, index);
      Check(atlas && atlas->alpha.size() == size_t(font::kFontAtlasExtent) * font::kFontAtlasExtent,
            "Missing episode high-resolution atlas");
      Check(atlas == font::FindVectorFontAtlas(set, index), "CPU atlas is not persistent");
    }
  auto* heap = memory.GetPhysicalHeap(); uint32_t payload = 0;
  Check(heap->Alloc(font::kFontIdentityBytes, 4096,
      memory::kMemoryAllocationReserve | memory::kMemoryAllocationCommit,
      memory::kMemoryProtectRead | memory::kMemoryProtectWrite, false, &payload), "Font payload allocation failed");
  GuestMemory guest(&memory);
  std::vector<uint8_t> stock(font::kFontIdentityBytes);
  for (size_t i = 0; i < stock.size(); ++i) stock[i] = uint8_t(i * 29 + 7);
  Check(guest.Write(payload, stock, true), "Font guest bytes unavailable");
  std::vector<uint8_t> legacy(16, 0); legacy.insert(legacy.end(), stock.begin(), stock.end());
  Check(font::StockFontIdentity(stock) == XXH3_64bits(legacy.data(), font::kFontIdentityBytes),
        "Font identity changed with upload alignment");
  xenos::xe_gpu_texture_fetch_t fetch{};
  fetch.type = xenos::FetchConstantType::kTexture; fetch.format = xenos::TextureFormat::k_DXT4_5;
  fetch.dimension = xenos::DataDimension::k2DOrStacked;
  fetch.base_address = payload >> 12; fetch.pitch = 16;
  fetch.size_2d.width = fetch.size_2d.height = 511;
  fetch.swizzle = xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA;
  fetch.mip_filter = xenos::TextureFilter::kBaseMap;
  NSString* shader = @R"MSL(#include <metal_stdlib>
using namespace metal;
kernel void sample_font(texture2d<float> image [[texture(0)]], sampler filter [[sampler(0)]],
                        device float4* output [[buffer(0)]], uint i [[thread_position_in_grid]]) {
  const uint mip = i / 8, point = i % 8;
  const uint2 extent(image.get_width(mip), image.get_height(mip));
  const uint2 pixel = uint2(point * 137 + 31, point * 59 + 17) % extent;
  output[i] = image.sample(filter, (float2(pixel) + 0.5f) / float2(extent), level(float(mip)));
})MSL";
  NSError* native_error = nil;
  auto library = [context->device newLibraryWithSource:shader options:nil error:&native_error];
  Check(library != nil, ui::metal::MetalError(native_error, "Font test shader failed"));
  auto pipeline = [context->device newComputePipelineStateWithFunction:[library newFunctionWithName:@"sample_font"] error:&native_error];
  Check(pipeline != nil, ui::metal::MetalError(native_error, "Font test pipeline failed"));
  ResourceStore resources(context, guest); std::string error;
  Check(cvar::SetFlagByName("gta4_native_vector_fonts", "true"), "Font setting unavailable");
  resources.BeginFrame(); size_t cases = 0;
  for (uint32_t id = 1; id <= 3; ++id) {
    const uint32_t handle = 0x71000000 + id * 64;
    auto commands = [context->queue commandBuffer];
    auto stock_image = resources.Texture(handle, fetch, commands, []{}, error);
    Check(stock_image && stock_image->image.width == 512 && !stock_image->font_replacement, error);
    resources.RegisterFont(handle, id);
    auto replacement = resources.Texture(handle, fetch, commands, []{}, error);
    Check(replacement && replacement->font_replacement && replacement->image.width == 2048 &&
          replacement->image.mipmapLevelCount == 5 && replacement->info.width == 511, error);
    size_t boundaries = 0;
    resources.RegisterFont(handle, 0);
    Check(resources.Texture(handle, fetch, commands, [&]{++boundaries;}, error) == replacement && boundaries == 0,
          "Font rebinding recreated or invalidated the atlas");
    auto view = resources.View(replacement, fetch, error);
    auto sampler = resources.Sampler(fetch, error, replacement.get());
    Check(view && sampler && view != replacement->image && view.parentTexture == replacement->image,
          "Font white-RGB coverage view not used");
    auto output = [context->device newBufferWithLength:5 * 8 * sizeof(float) * 4 options:MTLResourceStorageModeShared];
    auto encoder = [commands computeCommandEncoder];
    [encoder setComputePipelineState:pipeline]; [encoder setTexture:view atIndex:0];
    [encoder setSamplerState:sampler atIndex:0]; [encoder setBuffer:output offset:0 atIndex:0];
    [encoder dispatchThreads:MTLSizeMake(5 * 8, 1, 1) threadsPerThreadgroup:MTLSizeMake(8, 1, 1)];
    [encoder endEncoding];
    // Remove cache ownership before submission; commands retain this generation.
    resources.Release(handle);
    [commands commit]; [commands waitUntilCompleted];
    Check(commands.status == MTLCommandBufferStatusCompleted, ui::metal::MetalError(commands.error, "Font GPU test failed"));
    const auto* atlas = font::FindVectorFontAtlas(font::VectorFontSet::kGta4, id - 1);
    const auto mips = font::FontCoverageMips(atlas->alpha, 2048, 5);
    const auto* pixels = static_cast<const float*>(output.contents);
    for (size_t mip = 0; mip < 5; ++mip) for (size_t point = 0; point < 8; ++point) {
      const size_t extent = size_t(2048) >> mip;
      const size_t x = (point * 137 + 31) % extent, y = (point * 59 + 17) % extent;
      const auto* value = pixels + (mip * 8 + point) * 4;
      Check(value[0] == 1 && value[1] == 1 && value[2] == 1, "Font tint channels are not white");
      Check(std::abs(value[3] - float(mips[mip][y * extent + x]) / 255.0f) < 0.002f,
            "Font mip coverage or sampler clamp differs"); ++cases;
    }
    commands = [context->queue commandBuffer];
    auto released = resources.Texture(handle, fetch, commands, []{}, error);
    Check(released && !released->font_replacement, "Released font identity survived resource reuse");
    resources.RegisterFont(handle, id);
    Check(cvar::SetFlagByName("gta4_native_vector_fonts", "false"), "Cannot disable vector fonts");
    resources.BeginFrame();
    auto off = resources.Texture(handle, fetch, commands, []{}, error);
    Check(off && !off->font_replacement && off->image.width == 512, "Disabled font option still substitutes");
    Check(cvar::SetFlagByName("gta4_native_vector_fonts", "true"), "Cannot restore vector fonts");
    resources.BeginFrame();
    auto on = resources.Texture(handle, fetch, commands, []{}, error);
    Check(on && on->font_replacement, "Re-enabled font option does not restore substitution");
    [commands commit]; [commands waitUntilCompleted];
    Check(commands.status == MTLCommandBufferStatusCompleted, "Font toggle upload failed");
  }
  resources.Clear(); heap->Release(payload);
  std::printf("metal_font_atlases=passed sampled_cases=%zu episode_assets=9\n", cases);
}
