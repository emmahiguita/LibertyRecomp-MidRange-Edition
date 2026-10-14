// Resolves must publish their source without forcing unrelated attachment
// clears out to memory before those attachments can consume a clear load action.
std::vector<uint32_t> ResolveClearDependencyPixels(Fixture& f, uint32_t samples,
    bool tiled, bool resolve_clears, bool deferred) {
  const auto check_pixel = [&](const char* plane, size_t x, size_t y,
                               uint32_t actual, uint32_t expected) {
    if (actual == expected) { ++checks; return; }
    char detail[320];
    std::snprintf(detail, sizeof(detail),
        "Resolve clear dependency samples=%u tiled=%u side_effects=%u deferred=%u "
        "plane=%s x=%zu y=%zu actual=0x%08X expected=0x%08X",
        1u << samples, unsigned(tiled), unsigned(resolve_clears), unsigned(deferred),
        plane, x, y, actual, expected);
    Check(false, detail);
  };
  ConfigureGeometryFixture(f);
  using namespace rex::graphics::gta4_native::core;
  const auto source = f.Surface(false, samples);
  const auto other = f.Surface(false, samples), depth = f.Surface(true, samples);
  const auto source_snapshot = f.MakeTexture(false, tiled, xenos::Endian::k8in32);
  const auto other_snapshot = f.MakeTexture(false, tiled, xenos::Endian::k8in32);
  const auto depth_snapshot = f.MakeTexture(true, tiled, xenos::Endian::k8in32);
  f.Bind(source, {});
  f.Clear(1, {1, 0, 0, 1}, 0, 0);
  f.Bind(other, depth);
  f.Clear(0x31, {0, 0, 1, 1}, 0.75, 0x31);
  auto request = f.ResolveRequest(source, source_snapshot, false);
  if (resolve_clears) {
    // The resolve reads source, but the guest's clear side effects target the
    // currently bound other/depth attachments. Their earlier queued clears may
    // be superseded only if the new values remain visible to the next draw.
    request.flags |= 0x300u;
    request.clear_color_bits[1] = request.clear_color_bits[3] = std::bit_cast<uint32_t>(1.0f);
    request.clear_depth_bits = std::bit_cast<uint64_t>(0.25);
    request.clear_stencil = 0x63;
  }
  const auto before = f.renderer.statistics();
  f.Submit(request);
  const auto resolved = f.renderer.statistics();
  Check(resolved.clear_materializations - before.clear_materializations == (deferred ? 1u : 3u),
        "Resolve flushed unrelated clears or failed to materialize its source");

  f.Word(f.arena + kDepthEnableOffset, 1);
  f.Word(f.arena + kDepthStencilPackedOffset, 116);
  f.Word(f.arena + kPackedScissorTopLeftOffset, (4u << 16) | 4u);
  f.Word(f.arena + kPackedScissorBottomRightOffset, (12u << 16) | 12u);
  const std::array<GeometryVertex, 4> quad{GeometryCorner(-1, 1), GeometryCorner(1, 1),
      GeometryCorner(1, -1), GeometryCorner(-1, -1)};
  // The geometry fixture shader emits a UV gradient, not vertex color.
  // Zero RGB makes an exact opaque-black interior, distinct from both clear
  // guards, while retaining the real scissor, depth test and depth write.
  SubmitGeometry(f, quad, uint32_t(xenos::PrimitiveType::kQuadList), {0, 0, 0, 1});
  const auto drawn = f.renderer.statistics();
  if (deferred)
    Check(drawn.clear_load_folds - resolved.clear_load_folds == 2,
          "Unrelated color and depth clears did not reach the consuming draw");
  else if (!resolve_clears)
    Check(drawn.clear_load_folds == resolved.clear_load_folds,
          "Reference path unexpectedly retained the unrelated clears");

  f.Submit(f.ResolveRequest(other, other_snapshot, false));
  f.Submit(f.ResolveRequest(depth, depth_snapshot, true));
  auto result = f.Read(source_snapshot);
  for (size_t i = 0; i < result.size(); ++i)
    check_pixel("source", i % 16, i / 16, result[i], 0xFF0000FFu);
  const auto colors = f.Read(other_snapshot);
  const auto depths = f.Read(depth_snapshot);
  for (size_t y = 0; y < 16; ++y) for (size_t x = 0; x < 16; ++x) {
    const bool inner = x >= 4 && x < 12 && y >= 4 && y < 12;
    const uint32_t expected = inner ? 0xFF000000u : resolve_clears ? 0xFF00FF00u : 0xFFFF0000u;
    check_pixel("color", x, y, colors[y * 16 + x], expected);
    const float expected_depth = inner ? 0.5f : resolve_clears ? 0.25f : 0.75f;
    const uint32_t expected_stencil = resolve_clears ? 0x63u : 0x31u;
    const uint32_t expected_packed = (xenos::Float32To20e4(expected_depth, false) << 8) | expected_stencil;
    check_pixel("depth_stencil", x, y, depths[y * 16 + x], expected_packed);
  }
  result.insert(result.end(), colors.begin(), colors.end());
  result.insert(result.end(), depths.begin(), depths.end());
  std::printf("resolve_clear_dependencies samples=%u tiled=%u side_effects=%u deferred=%u materialized=%llu folded=%llu\n",
      1u << samples, unsigned(tiled), unsigned(resolve_clears), unsigned(deferred),
      drawn.clear_materializations - before.clear_materializations,
      drawn.clear_load_folds - before.clear_load_folds);
  return result;
}

void ExercisePendingResolveOwner(Fixture& f, bool tiled) {
  // Resolve through an aliased guest view whose latest owner, rather than the
  // handle in the request, still carries a pending clear.
  const auto owner = f.Surface(false, 0, 16, 32);
  auto requested = f.Surface(false, 1);
  requested.address = (owner.address & 0x7FFu) | 0x00030000u;
  f.Word(requested.handle + 28, requested.address);
  GuestSurfaceView a{}, b{};
  Check(DecodeGuestSurfaceView(owner, false, a) && DecodeGuestSurfaceView(requested, false, b) &&
        GetGuestPlacementKey(a) == GetGuestPlacementKey(b), "Pending resolve owner fixture does not alias");
  const auto unrelated = f.Surface(false, 0);
  f.Bind(owner, {}); f.Clear(1, {1, 0, 0, 1}, 0, 0, {0, 0, 16, 32});
  f.Bind(unrelated, {}); f.Clear(1, {0, 1, 0, 1}, 0, 0);
  const auto snapshot = f.MakeTexture(false, tiled, xenos::Endian::k8in32);
  const auto before = f.renderer.statistics();
  f.Submit(f.ResolveRequest(requested, snapshot, false));
  Check(f.renderer.statistics().clear_materializations - before.clear_materializations == 1,
        "Resolve did not selectively materialize the chosen aliased owner");
  for (uint32_t pixel : f.Read(snapshot))
    Check(pixel == 0xFF0000FFu, "Pending aliased source resolved undefined or unrelated contents");
  std::printf("resolve_pending_owner tiled=%u passed=true\n", unsigned(tiled));
}

void ExerciseResolveClearContinuity(Fixture& f, uint32_t samples, bool tiled) {
  ConfigureGeometryFixture(f);
  const auto source = f.Surface(false, samples), active = f.Surface(false, samples);
  const auto unrelated = f.Surface(false, samples);
  const auto snapshot = f.MakeTexture(false, tiled, xenos::Endian::k8in32);
  f.Bind(source, {}); f.Clear(1, {1, 0, 0, 1}, 0, 0);
  const auto request = f.ResolveRequest(source, snapshot, false);
  f.Submit(request);
  f.Bind(unrelated, {}); f.Clear(1, {0, 1, 0, 1}, 0, 0);
  f.Bind(active, {}); f.Clear(1, {0, 0, 1, 1}, 0, 0);
  const std::array<GeometryVertex, 4> quad{GeometryCorner(-1, 1), GeometryCorner(1, 1),
      GeometryCorner(1, -1), GeometryCorner(-1, -1)};
  SubmitGeometry(f, quad, uint32_t(xenos::PrimitiveType::kQuadList), {1, 1, 1, 1});
  const auto before = f.renderer.statistics();
  f.Submit(request);
  SubmitGeometry(f, quad, uint32_t(xenos::PrimitiveType::kQuadList), {1, 1, 1, 1});
  const auto after = f.renderer.statistics();
  Check(after.resolve_skips == before.resolve_skips + 1, "Continuity resolve was not reused");
  Check(after.clear_materializations == before.clear_materializations,
        "No-match resolve drained an unrelated pending clear");
  Check(after.render_passes_created == before.render_passes_created,
        "No-match pending clear search broke compatible draw encoding");
  for (uint32_t pixel : f.Read(snapshot))
    Check(pixel == 0xFF0000FFu, "No-match resolve changed the prior snapshot");
  // Readback/flush must still drain pending clears that have no consuming draw.
  const auto untouched = f.MakeTexture(false, tiled, xenos::Endian::k8in32);
  f.Submit(f.ResolveRequest(unrelated, untouched, false));
  for (uint32_t pixel : f.Read(untouched))
    Check(pixel == 0xFF00FF00u, "Deferred attachment clear was lost at flush");
  std::printf("resolve_clear_continuity samples=%u tiled=%u passed=true\n", 1u << samples, unsigned(tiled));
}

void ExerciseResolveClearDependencies(memory::Memory& memory,
    const std::shared_ptr<ui::metal::MetalContext>& context, const std::filesystem::path& shaders) {
  constexpr const char* policies[] = {"gta4_metal_fold_full_clears", "gta4_metal_defer_unrelated_clears",
                                    "gta4_native_anti_aliasing"};
  std::array<std::string, 3> saved{};
  for (size_t i = 0; i < saved.size(); ++i) saved[i] = cvar::GetFlagByName(policies[i]);
  Check(cvar::SetFlagByName(policies[0], "true"), "Enable queued clears for dependency cases");
  for (uint32_t samples = 0; samples < 3; ++samples) for (bool tiled : {false, true}) {
    Check(cvar::SetFlagByName(policies[2], samples == 0 ? "off" : samples == 1 ? "msaa2x" : "msaa4x"),
          "Select resolve dependency AA topology");
    for (bool resolve_clears : {false, true}) {
      std::vector<uint32_t> reference;
      for (bool deferred : {false, true}) {
        Check(cvar::SetFlagByName(policies[1], deferred ? "true" : "false"), "Resolve clear toggle missing");
        Fixture fixture(memory, context, shaders);
        auto pixels = ResolveClearDependencyPixels(fixture, samples, tiled, resolve_clears, deferred);
        if (!deferred) reference = std::move(pixels);
        else {
          Check(reference.size() == pixels.size(), "Resolve clear dependency output extent changed");
          for (size_t i = 0; i < pixels.size(); ++i) {
            if (reference[i] != pixels[i]) {
              char detail[320];
              std::snprintf(detail, sizeof(detail),
                  "Selective clear differential samples=%u tiled=%u side_effects=%u deferred=1 "
                  "plane=%zu x=%zu y=%zu actual=0x%08X expected=0x%08X",
                  1u << samples, unsigned(tiled), unsigned(resolve_clears),
                  i / (16u * 16u), i % 16u, (i / 16u) % 16u, pixels[i], reference[i]);
              Check(false, detail);
            }
            ++checks;
          }
        }
      }
    }
    Fixture continuity(memory, context, shaders);
    ExerciseResolveClearContinuity(continuity, samples, tiled);
  }
  Check(cvar::SetFlagByName(policies[2], "off"), "Select single-sample aliased owner");
  Check(cvar::SetFlagByName(policies[1], "true"), "Enable selective aliased owner case");
  for (bool tiled : {false, true}) {
    Fixture fixture(memory, context, shaders);
    ExercisePendingResolveOwner(fixture, tiled);
  }
  for (size_t i = 0; i < saved.size(); ++i)
    Check(cvar::SetFlagByName(policies[i], saved[i]), "Restore resolve dependency flags");
  std::puts("resolve_clear_dependency_gpu_differential=passed bit_identical=true");
}
