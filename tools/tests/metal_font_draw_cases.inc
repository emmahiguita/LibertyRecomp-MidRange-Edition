// Real GTA IV immediate-mode shaders, title commands, font registration,
// production argument buffers, blending/alpha test, resolve and readback.

void ExerciseFontDraws(Fixture& f) {
  using namespace rex::graphics::gta4_native::core;
  ConfigureGeometryFixture(f, 0x672D97058AC7790Bull, 0xD72B5CF469E02C54ull);
  RegisterShaderCommand vs{}; vs.shader = 1; vs.hash = 0x672D97058AC7790Bull; vs.stage = ShaderStage::kVertex;
  RegisterShaderCommand ps{}; ps.shader = 2; ps.hash = 0xD72B5CF469E02C54ull; ps.stage = ShaderStage::kPixel;
  f.Submit(vs); f.Submit(ps);
  // The stock VS uses c8-c11 as its matrix. Translate away only the shared
  // half-pixel adjustment to cover this test target's exact pixel boundaries.
  for (uint32_t row = 0; row < 4; ++row) for (uint32_t column = 0; column < 4; ++column) {
    float value = row == column ? 1.0f : 0.0f;
    if (row == 3 && column == 0) value = -1.0f / 16.0f;
    if (row == 3 && column == 1) value = 1.0f / 16.0f;
    f.Word(f.arena + 0x780 + (8 + row) * 16 + column * 4, std::bit_cast<uint32_t>(value));
  }
  for (uint32_t offset : {624u, 628u, 736u}) f.Word(f.arena + 0x1780 + offset, std::bit_cast<uint32_t>(1.0f));
  const auto surface = f.Surface(false, 0); f.Bind(surface, {});
  const auto destination = f.MakeTexture(false, false, xenos::Endian::k8in32);
  const uint32_t handle = f.arena + f.next_header; f.next_header += 64;
  const uint32_t payload = f.backing + f.next_backing; f.next_backing += kFontIdentityBytes;
  auto stock = f.guest.Writable(payload, kFontIdentityBytes, true);
  Check(stock.size() == kFontIdentityBytes, "Font draw stock allocation unavailable");
  std::fill(stock.begin(), stock.end(), uint8_t{0});
  xenos::xe_gpu_texture_fetch_t fetch{};
  fetch.type = xenos::FetchConstantType::kTexture; fetch.format = xenos::TextureFormat::k_DXT4_5;
  fetch.dimension = xenos::DataDimension::k2DOrStacked; fetch.base_address = payload >> 12;
  fetch.pitch = 16; fetch.size_2d.width = fetch.size_2d.height = 511;
  fetch.swizzle = xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA; fetch.mip_filter = xenos::TextureFilter::kBaseMap;
  const auto words = std::bit_cast<std::array<uint32_t, 6>>(fetch);
  for (size_t i = 0; i < words.size(); ++i) {
    f.Word(handle + 28 + uint32_t(i * sizeof(uint32_t)), words[i]);
    f.Word(f.arena + 0x480 + uint32_t(i * sizeof(uint32_t)), words[i]);
  }
  f.Word(f.arena + 0x30F8, handle);
  size_t cases = 0;
  for (uint32_t id = 1; id <= 3; ++id) {
    const auto* atlas = FindVectorFontAtlas(VectorFontSet::kGta4, id - 1);
    Check(atlas != nullptr, "Font draw atlas not loaded");
    SetTextureCommand bind{}; bind.device = f.arena; bind.texture = handle; bind.vector_font_id = id; f.Submit(bind);
    // A later ordinary bind must not erase the font owner metadata.
    bind.vector_font_id = 0; f.Submit(bind);
    for (bool opaque : {false, true}) {
      const auto sample = std::find_if(atlas->alpha.begin(), atlas->alpha.end(), [=](uint8_t a) { return opaque ? a == 255 : a == 0; });
      Check(sample != atlas->alpha.end(), "Font draw coverage test point unavailable");
      const size_t index = size_t(sample - atlas->alpha.begin());
      const std::array<float, 2> uv{(float(index % kFontAtlasExtent) + 0.5f) / kFontAtlasExtent,
                                  (float(index / kFontAtlasExtent) + 0.5f) / kFontAtlasExtent};
      for (bool alpha_test : {false, true}) {
        f.Word(f.arena + kAlphaTestFunctionPackedOffset, alpha_test ? (8u | 4u) : 7u);
        f.Word(f.arena + kAlphaReferenceOffset, std::bit_cast<uint32_t>(0.5f));
        for (auto primitive : {xenos::PrimitiveType::kRectangleList, xenos::PrimitiveType::kQuadList}) {
          std::array<GeometryVertex, 4> vertices{GeometryCorner(-1,1),GeometryCorner(1,1),GeometryCorner(-1,-1),GeometryCorner(1,-1)};
          for (auto& v : vertices) {v.color = {0.25f,0.5f,0.75f,1}; v.uv = uv;}
          if (primitive == xenos::PrimitiveType::kQuadList) std::swap(vertices[2],vertices[3]);
          f.Clear(1, {}, 0, 0);
          const auto before = f.renderer.statistics().draws;
          SubmitGeometry(f, std::span(vertices).first(primitive == xenos::PrimitiveType::kRectangleList ? 3 : 4), uint32_t(primitive), {1,1,1,1});
          Check(f.renderer.statistics().draws == before + 1, "Font draw was rejected or skipped");
          f.Submit(f.ResolveRequest(surface, destination, false));
          const auto pixels = f.Read(destination);
          for (const auto value : pixels) {
            const uint32_t expected = !opaque && alpha_test ? 0u : (opaque ? 0xFFBF8040u : 0x00BF8040u);
            for (uint32_t channel = 0; channel < 4; ++channel) {
              const int actual = int((value >> (channel * 8)) & 255u);
              const int wanted = int((expected >> (channel * 8)) & 255u);
              Check(std::abs(actual - wanted) <= 1, "Font draw pixel disagrees with coverage/vertex tint");
            }
          }
          ++cases;
        }
      }
    }
    ReleaseResourceCommand release{}; release.resource = handle; f.Submit(release);
  }
  Check(f.renderer.Finish(f.error), f.error);
  std::printf("metal_font_title_draws=passed cases=%zu stock_vs=%016llX stock_ps=%016llX\n",
              cases, static_cast<unsigned long long>(vs.hash), static_cast<unsigned long long>(ps.hash));
}
