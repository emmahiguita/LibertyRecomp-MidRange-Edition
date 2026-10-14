void ExerciseConstantTracking(Fixture& f) {
  ConfigureGeometryFixture(f);
  const auto surface = f.Surface(false, 0);
  const auto texture = f.MakeTexture(false, false, xenos::Endian::k8in32);
  f.Bind(surface, {});
  const std::array<GeometryVertex, 4> quad{GeometryCorner(-1, 1), GeometryCorner(1, 1),
      GeometryCorner(1, -1), GeometryCorner(-1, -1)};
  const std::array<float, 4> full{1, 1, 1, 1}, changed{0.25f, 0.5f, 0.75f, 1};
  DrawPrimitiveUpCommand draw{}; draw.device = f.arena;
  draw.primitive_type = uint32_t(xenos::PrimitiveType::kQuadList);
  draw.vertex_count = uint32_t(quad.size()); draw.stride = sizeof(GeometryVertex);
  draw.vertex_data = f.arena + 32768; draw.vertex_data_size = sizeof(quad);
  auto set_constants = [&](const auto& value) {
    for (size_t i = 0; i < value.size(); ++i)
      f.Word(f.arena + 0x1780 + uint32_t(i * sizeof(float)), std::bit_cast<uint32_t>(value[i]));
  };
  f.Clear(1, {}, 0, 0);
  SubmitGeometry(f, quad, draw.primitive_type, full);
  auto before = f.renderer.statistics();
  f.Submit(draw);
  auto after = f.renderer.statistics();
  Check(after.guest_constant_skips > before.guest_constant_skips,
        "Unchanged float banks did not bypass constant processing");
  Check(after.guest_constant_audits > before.guest_constant_audits,
        "Clean-bank reuse was not checked by the audit");
  CheckGeometryPixels(f, surface, texture, full, "unchanged bank reuse");

  // Establish old banks within this submission before every invalidation case.
  for (unsigned boundary = 0; boundary < 3; ++boundary) {
    SubmitGeometry(f, quad, draw.primitive_type, full);
    set_constants(changed);
    if (boundary == 0) {
      auto empty = draw; empty.vertex_count = 0; f.Submit(empty);
    } else if (boundary == 1) {
      f.Clear(1, {}, 0, 0);
    } else {
      auto replay = draw; replay.dirty_state.words.fill(UINT64_MAX); f.Submit(replay);
    }
    f.Submit(draw);
    CheckGeometryPixels(f, surface, texture, changed, "dirty state across command boundary");
  }
  Check(f.renderer.statistics().guest_constant_mismatches == 0,
        "Correct producer dirty masks failed their audit");

  // Deliberately violate the producer contract: audit must repair THIS draw and
  // keep the renderer on the exact-snapshot path for the remainder of the run.
  SubmitGeometry(f, quad, draw.primitive_type, full);
  set_constants(changed); f.pending_constants = {};
  f.Submit(draw);
  Check(f.renderer.statistics().guest_constant_mismatches == 1,
        "Unreported guest write was not caught");
  CheckGeometryPixels(f, surface, texture, changed, "audit repairs unreported write");
  SubmitGeometry(f, quad, draw.primitive_type, full);
  before = f.renderer.statistics();
  f.Submit(draw);
  Check(f.renderer.statistics().guest_constant_skips == before.guest_constant_skips,
        "Failed dirty contract did not retain its safe fallback");
  CheckGeometryPixels(f, surface, texture, full, "exact snapshot fallback");
  std::puts("metal_constant_tracking_pixels=passed");
}
