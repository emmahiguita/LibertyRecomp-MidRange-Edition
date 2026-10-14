// Included by metal_renderer_gpu_test.mm: exercises the production draw path,
// not a separate test-only geometry encoder.
struct GeometryVertex {
  std::array<float, 4> position;
  std::array<float, 4> color;
  std::array<float, 2> uv;
};

void ConfigureGeometryFixture(Fixture& f, uint64_t vertex_hash = 1, uint64_t pixel_hash = 2) {
  RegisterShaderCommand vertex{}; vertex.shader = 1; vertex.hash = vertex_hash; vertex.stage = ShaderStage::kVertex;
  RegisterShaderCommand pixel{}; pixel.shader = 2; pixel.hash = pixel_hash; pixel.stage = ShaderStage::kPixel;
  f.Submit(vertex); f.Submit(pixel);
  RegisterVertexDeclarationCommand declaration{};
  declaration.device = f.arena; declaration.declaration = 3; declaration.element_count = 3;
  declaration.elements[0] = {0, uint16_t(offsetof(GeometryVertex, position)), 0x1A23A6, 0, 0, 0, 0};
  declaration.elements[1] = {0, uint16_t(offsetof(GeometryVertex, color)), 0x1A23A6, 0, 10, 0, 0};
  declaration.elements[2] = {0, uint16_t(offsetof(GeometryVertex, uv)), 0x2C23A5, 0, 5, 0, 0};
  f.Submit(declaration);
  SetShaderCommand bind{}; bind.device = f.arena;
  bind.header = {sizeof(bind), CommandType::kSetVertexShader}; bind.shader = 1; f.Submit(bind);
  bind.header = {sizeof(bind), CommandType::kSetPixelShader}; bind.shader = 2; f.Submit(bind);
  SetVertexDeclarationCommand set{}; set.device = f.arena; set.declaration = 3; f.Submit(set);
  using namespace rex::graphics::gta4_native::core;
  f.Word(f.arena + kClipControlPackedOffset, kNativeDirectXClipSpaceMask);
  f.Word(f.arena + kSampleMaskOffset, 0xFFFF);
  f.Word(f.arena + kPackedColorWriteMaskOffset, 0xF);
  f.Word(f.arena + kHardwareBlendPackedOffsets[0], 1u | (1u << 16));
  f.Word(f.arena + kPackedScissorBottomRightOffset, (16u << 16) | 16u);
  const std::array<float, 6> viewport{0, 0, 16, 16, 0, 1};
  for (size_t i = 0; i < viewport.size(); ++i)
    f.Word(f.arena + kViewportXOffset + uint32_t(i * sizeof(float)), std::bit_cast<uint32_t>(viewport[i]));
}

GeometryVertex GeometryCorner(float x, float y) {
  return {{x, y, 0.5f, 1.0f}, {1, 0, 0, 1}, {(x + 1.0f) * 0.5f, (1.0f - y) * 0.5f}};
}

void SubmitGeometry(Fixture& f, std::span<const GeometryVertex> vertices, uint32_t topology,
                    const std::array<float, 4>& multiplier) {
  const uint32_t payload = f.arena + 32768;
  const auto words = std::span(reinterpret_cast<const uint32_t*>(vertices.data()), vertices.size_bytes() / sizeof(uint32_t));
  for (size_t i = 0; i < words.size(); ++i) f.Word(payload + uint32_t(i * sizeof(uint32_t)), words[i]);
  for (size_t i = 0; i < multiplier.size(); ++i)
    f.Word(f.arena + 0x1780 + uint32_t(i * sizeof(float)), std::bit_cast<uint32_t>(multiplier[i]));
  DrawPrimitiveUpCommand draw{}; draw.device = f.arena;
  draw.primitive_type = topology; draw.vertex_count = uint32_t(vertices.size());
  draw.stride = sizeof(GeometryVertex); draw.vertex_data = payload; draw.vertex_data_size = uint32_t(vertices.size_bytes());
  f.Submit(draw);
}

void CheckGeometryPixels(Fixture& f, const SurfaceDescriptor& surface, const Fixture::Texture& texture,
                         const std::array<float, 4>& multiplier, const char* label) {
  f.Submit(f.ResolveRequest(surface, texture, false));
  const auto pixels = f.Read(texture);
  size_t uncovered = 0;
  for (size_t y = 0; y < 16; ++y) for (size_t x = 0; x < 16; ++x) {
    const uint32_t value = pixels[y * 16 + x];
    if (!(value >> 24)) ++uncovered;
    const std::array<float, 4> expected{(float(x) + 0.5f) / 16.0f, (float(y) + 0.5f) / 16.0f, 1, 1};
    for (size_t channel = 0; channel < expected.size(); ++channel) {
      const int wanted = int(std::lround(expected[channel] * multiplier[channel] * 255.0f));
      const int actual = int((value >> (channel * 8)) & 255u);
      if (std::abs(actual - wanted) > 1)
        throw std::runtime_error(std::string(label) + " pixel=" + std::to_string(x) + "," + std::to_string(y) +
            " channel=" + std::to_string(channel) + " expected=" + std::to_string(wanted) + " actual=" + std::to_string(actual));
      ++checks;
    }
  }
  Check(uncovered == 0, "Geometry left a triangular region uncovered");
}

void ExerciseGeometryDraws(Fixture& f, bool scaled) {
  ConfigureGeometryFixture(f);
  const auto surface = f.Surface(false, 0);
  if (scaled) {
    RegisterVirtualResourceCommand registration{};
    registration.resource = surface.handle; registration.kind = VirtualResourceKind::kSurface;
    registration.guest_backing_width = registration.guest_backing_height = 16;
    registration.logical_width = registration.logical_height = 16;
    registration.physical_width = registration.physical_height = 32;
    f.Submit(registration);
  }
  f.Bind(surface, {});
  const auto texture = f.MakeTexture(false, false, xenos::Endian::k8in32);
  const std::array<float, 4> full{1, 1, 1, 1};
  const std::array<GeometryVertex, 4> quad{GeometryCorner(-1, 1), GeometryCorner(1, 1),
                                          GeometryCorner(1, -1), GeometryCorner(-1, -1)};
  f.Clear(1, {}, 0, 0); SubmitGeometry(f, quad, uint32_t(xenos::PrimitiveType::kQuadList), full);
  CheckGeometryPixels(f, surface, texture, full, "quad control");
  const auto before_registration = f.renderer.statistics();
  for (uint32_t repeat = 0; repeat < 8; ++repeat) {
    ConfigureGeometryFixture(f);
    f.Clear(1, {}, 0, 0);
    SubmitGeometry(f, quad, uint32_t(xenos::PrimitiveType::kQuadList), full);
  }
  const auto after_registration = f.renderer.statistics();
  Check(after_registration.declaration_definitions == before_registration.declaration_definitions,
        "Identical vertex declaration registration created a new layout identity");
  Check(after_registration.pipeline_creations == before_registration.pipeline_creations,
        "Identical vertex declaration registration rebuilt the Metal pipeline");
  Check(after_registration.declaration_reuses == before_registration.declaration_reuses + 8,
        "Repeated declarations did not use their existing definition");
  CheckGeometryPixels(f, surface, texture, full, "repeated registration coverage");
  RegisterVertexDeclarationCommand changed{};
  changed.device = f.arena; changed.declaration = 3; changed.element_count = 3;
  changed.elements[0] = {0, uint16_t(offsetof(GeometryVertex, position)), 0x1A23A6, 0, 0, 0, 0};
  changed.elements[1] = {0, uint16_t(offsetof(GeometryVertex, color)), 0x1A23A6, 0, 10, 0, 0};
  changed.elements[2] = {0, uint16_t(offsetof(GeometryVertex, uv)), 0x2C23A5, 0, 5, 0, 0};
  const auto check_real_change = [&](const RegisterVertexDeclarationCommand& command) {
    const auto before = f.renderer.statistics();
    f.Submit(command);
    Check(f.renderer.statistics().declaration_definitions == before.declaration_definitions + 1,
          "Changed vertex declaration retained a stale identity");
    ConfigureGeometryFixture(f);
  };
  auto invalid = changed; invalid.elements[0].stream = kVertexStreamCount;
  const auto before_invalid = f.renderer.statistics();
  Check(!f.renderer.Submit(Bytes(invalid), f.error), "Invalid repeated declaration was accepted");
  Check(f.renderer.statistics().declaration_definitions == before_invalid.declaration_definitions,
        "Rejected declaration replaced the valid layout");
  for (size_t field = 0; field < 6; ++field) {
    auto variant = changed;
    switch (field) {
      case 0: variant.maximum_stream = variant.elements[2].stream = 1; break;
      case 1: variant.elements[2].offset = 0; break;
      case 2: variant.elements[2].type = 0x1A23A6; break;
      case 3: variant.elements[2].method = 1; break;
      case 4: variant.elements[2].usage = 3; break;
      case 5: variant.elements[2].usage_index = 1; break;
    }
    check_real_change(variant);
  }
  auto padding_only = changed; padding_only.elements[0].padding = 0xFF;
  const auto before_padding = f.renderer.statistics(); f.Submit(padding_only);
  Check(f.renderer.statistics().declaration_definitions == before_padding.declaration_definitions,
        "Vertex padding bytes invalidated a semantic layout");
  ReleaseResourceCommand release{}; release.resource = 3; f.Submit(release);
  const auto before_recreate = f.renderer.statistics(); ConfigureGeometryFixture(f);
  Check(f.renderer.statistics().declaration_definitions == before_recreate.declaration_definitions + 1,
        "Released layout identity was reused for a new resource");
  f.Clear(1, {}, 0, 0); SubmitGeometry(f, quad, uint32_t(xenos::PrimitiveType::kQuadList), full);
  CheckGeometryPixels(f, surface, texture, full, "recreated declaration coverage");
  const auto pass_before = f.renderer.statistics();
  f.Clear(1, {}, 0, 0);
  SubmitGeometry(f, quad, uint32_t(xenos::PrimitiveType::kQuadList), full);
  RenderPhaseMarkerCommand phase{}; phase.phase = RenderPhase::kLightSetup; phase.event = RenderPhaseEvent::kBegin;
  f.Submit(phase);
  SubmitGeometry(f, quad, uint32_t(xenos::PrimitiveType::kQuadList), full);
  phase.event = RenderPhaseEvent::kEnd; f.Submit(phase);
  SubmitGeometry(f, quad, uint32_t(xenos::PrimitiveType::kQuadList), full);
  Check(f.renderer.statistics().render_passes_created == pass_before.render_passes_created + 1,
        "Profiling phase markers split an otherwise unchanged render pass");
  CheckGeometryPixels(f, surface, texture, full, "continuous pass across markers");
  // Disabling depth or color writes does not unbind the title's attachment.
  // Keep the pass if only pipeline write/test state changed, then prove that
  // writes really remain disabled and an actual target change still splits it.
  const auto depth = f.Surface(true, 0);
  if (scaled) {
    RegisterVirtualResourceCommand registration{};
    registration.resource = depth.handle; registration.kind = VirtualResourceKind::kSurface;
    registration.guest_backing_width = registration.guest_backing_height = 16;
    registration.logical_width = registration.logical_height = 16;
    registration.physical_width = registration.physical_height = 32;
    f.Submit(registration);
  }
  f.Bind(surface, depth); f.Clear(0x31, {}, 0, 0);
  using namespace rex::graphics::gta4_native::core;
  f.Word(f.arena + kDepthStencilPackedOffset, (7u << 4) | 4u);
  f.Word(f.arena + kDepthEnableOffset, 1);
  const auto before_toggles = f.renderer.statistics();
  SubmitGeometry(f, quad, uint32_t(xenos::PrimitiveType::kQuadList), full);
  f.Word(f.arena + kDepthEnableOffset, 0);
  SubmitGeometry(f, quad, uint32_t(xenos::PrimitiveType::kQuadList), full);
  f.Word(f.arena + kDepthEnableOffset, 1);
  f.Word(f.arena + kPackedColorWriteMaskOffset, 0);
  SubmitGeometry(f, quad, uint32_t(xenos::PrimitiveType::kQuadList), {0, 0, 0, 0});
  f.Word(f.arena + kPackedColorWriteMaskOffset, 0xF);
  Check(f.renderer.statistics().render_passes_created == before_toggles.render_passes_created + 1,
        "Inactive but still bound attachments caused redundant store/load passes");
  CheckGeometryPixels(f, surface, texture, full, "disabled color write preservation");
  // A draw with every output disabled still uses the same bound surfaces.
  // It must not detach color and then reload it for the next visible draw.
  f.Bind(surface, {}); f.Word(f.arena + kDepthEnableOffset, 0);
  const auto before_no_output = f.renderer.statistics();
  SubmitGeometry(f, quad, uint32_t(xenos::PrimitiveType::kQuadList), full);
  f.Word(f.arena + kPackedColorWriteMaskOffset, 0);
  SubmitGeometry(f, quad, uint32_t(xenos::PrimitiveType::kQuadList), {0, 0, 0, 0});
  f.Word(f.arena + kPackedColorWriteMaskOffset, 0xF);
  SubmitGeometry(f, quad, uint32_t(xenos::PrimitiveType::kQuadList), full);
  Check(f.renderer.statistics().render_passes_created == before_no_output.render_passes_created + 1,
        "Fully masked draw detached and reloaded unchanged bound attachments");
  CheckGeometryPixels(f, surface, texture, full, "fully masked draw preservation");
  const auto other_surface = f.Surface(false, 0);
  f.Bind(other_surface, {}); f.Word(f.arena + kDepthEnableOffset, 0);
  const auto before_target_change = f.renderer.statistics();
  SubmitGeometry(f, quad, uint32_t(xenos::PrimitiveType::kQuadList), full);
  Check(f.renderer.statistics().render_passes_created == before_target_change.render_passes_created + 1,
        "A changed attachment incorrectly reused an earlier pass");
  f.Bind(surface, {});
  SubmitGeometry(f, quad, uint32_t(xenos::PrimitiveType::kQuadList), full);
  Check(f.renderer.statistics().render_passes_created == before_target_change.render_passes_created + 2,
        "Switching back to an earlier target failed to restore its render pass");
  CheckGeometryPixels(f, surface, texture, full, "restored target coverage");
  // Every ordering of the three supplied corners must reconstruct the fourth.
  const std::array<GeometryVertex, 3> corners{quad[0], quad[1], quad[3]};
  std::array<size_t, 3> order{0, 1, 2};
  do {
    const std::array<GeometryVertex, 3> rectangle{corners[order[0]], corners[order[1]], corners[order[2]]};
    f.Clear(1, {}, 0, 0);
    SubmitGeometry(f, rectangle, uint32_t(xenos::PrimitiveType::kRectangleList), full);
    CheckGeometryPixels(f, surface, texture, full, "rectangle coverage and interpolation");
  } while (std::next_permutation(order.begin(), order.end()));
  // Consecutive rectangles must not be joined by strip triangles.
  const std::array<GeometryVertex, 6> split{GeometryCorner(-1, 1), GeometryCorner(0, 1), GeometryCorner(-1, -1),
      GeometryCorner(0, 1), GeometryCorner(1, 1), GeometryCorner(0, -1)};
  for (const auto& value : {full, std::array<float, 4>{0.5f, 0.25f, 0.75f, 1}, full}) {
    f.Clear(1, {}, 0, 0);
    SubmitGeometry(f, split, uint32_t(xenos::PrimitiveType::kRectangleList), value);
    CheckGeometryPixels(f, surface, texture, value, "independent rectangles and changed constants");
  }
  Check(f.renderer.Finish(f.error), f.error);
  std::printf("geometry_draws scaled=%u passed=true\n", unsigned(scaled));
}
