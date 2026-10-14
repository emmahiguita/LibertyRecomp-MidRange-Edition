#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/graphics/gta4_native/surface_view.h>
#include <rex/graphics/pipeline/texture/conversion.h>
#include <rex/graphics/pipeline/texture/util.h>
#include "graphics/gta4_metal/renderer.h"
#include "graphics/gta4_metal/shaders.h"
#include "graphics/gta4_metal/resources.h"
#include "graphics/gta4_native/core/font_atlas.h"
#include "ui/metal/context.h"
#include "ui/metal/guest_output_context.h"
#include "graphics/gta4_metal/post_processing.h"

// The production app defines this canonical frontend setting; the headless test
// provides the same name so it can exercise each restart-scoped scene topology.
REXCVAR_DEFINE_STRING(gta4_native_anti_aliasing, "off", "Test", "Scene topology under test");

namespace {
using namespace rex;
using namespace rex::graphics;
using namespace rex::graphics::gta4_native;
using namespace rex::graphics::gta4_metal;
size_t checks = 0;
void Check(bool result, const std::string& message) {
  ++checks;
  if (!result) throw std::runtime_error(message);
}
template<class T> std::span<const std::byte> Bytes(const T& value) { return std::as_bytes(std::span(&value, 1)); }
struct Fixture {
  memory::Memory& memory;
  GuestMemory guest;
  std::shared_ptr<ui::metal::MetalContext> context;
  Renderer renderer;
  uint32_t arena = 0, backing = 0, next_header = 65536, next_backing = 0;
  std::string error;
  NativeDirtyState pending_constants{};
  struct Texture { uint32_t handle; xenos::xe_gpu_texture_fetch_t fetch; TextureInfo info; };

  Fixture(memory::Memory& m, std::shared_ptr<ui::metal::MetalContext> c, const std::filesystem::path& shaders, ui::Presenter* presenter = nullptr)
      : memory(m), guest(&m), context(std::move(c)), renderer(context, &m, presenter) {
    const uint32_t allocation = memory::kMemoryAllocationReserve | memory::kMemoryAllocationCommit;
    const uint32_t protection = memory::kMemoryProtectRead | memory::kMemoryProtectWrite;
    Check(memory.LookupHeap(0x10000000)->Alloc(131072, 4096, allocation, protection, false, &arena), "Header allocation failed");
    Check(memory.GetPhysicalHeap()->Alloc(2u * 1024u * 1024u, 4096, allocation, protection, false, &backing), "Texture backing allocation failed");
    // Guest heaps may return pages from a previous fixture. Tests must establish
    // a fresh device snapshot, rather than inherit font/coverage state from it.
    auto device_bytes=guest.Writable(arena,kGuestDeviceSize);
    Check(device_bytes.size()==kGuestDeviceSize,"Device fixture span unavailable");
    std::memset(device_bytes.data(),0,device_bytes.size());
    Check(renderer.Initialize(error, shaders), "Renderer initialization: " + error);
    DeviceCommand device{}; device.header = {sizeof(device), CommandType::kDeviceCreated}; device.device = arena;
    Submit(device);
  }
  ~Fixture() {
    renderer.Finish(error);
    if (arena) memory.LookupHeap(arena)->Release(arena);
    if (backing) memory.GetPhysicalHeap()->Release(backing);
  }
  void Word(uint32_t address, uint32_t word) {
    // Match the native producer: writes survive until a draw or clear consumes them.
    if (address >= arena + 0x780 && address < arena + 0x1780) pending_constants.words[0] = UINT64_MAX;
    if (address >= arena + 0x1780 && address < arena + 0x2580) pending_constants.words[1] = UINT64_MAX;
    word = __builtin_bswap32(word);
    Check(guest.Write(address, {reinterpret_cast<const uint8_t*>(&word), sizeof(word)}), "Guest word store failed");
  }
  template<class T> void Submit(const T& command) {
    auto submitted = command;
    if constexpr (requires { submitted.dirty_state; }) {
      for (size_t i = 0; i < pending_constants.words.size(); ++i)
        submitted.dirty_state.words[i] |= pending_constants.words[i];
      pending_constants = {};
    }
    if (!renderer.Submit(Bytes(submitted), error)) throw std::runtime_error("Command " + std::to_string(uint32_t(command.header.type)) + ": " + error);
    ++checks;
  }
  SurfaceDescriptor Surface(bool depth, uint32_t sample_type, uint32_t width = 16, uint32_t height = 16) {
    const uint32_t handle = arena + next_header; next_header += 64;
    SurfaceDescriptor result{};
    result.handle = handle; result.width = width; result.height = height;
    result.base = width | (sample_type << 16); result.address = handle & 2047u;
    result.sample_type = sample_type; result.format = depth ? 0x1A220197u : 0x18280186u;
    result.packed_dimensions = ((result.width - 1) << 18) | ((result.height - 1) << 3);
    Word(handle, result.flags); Word(handle + 24, result.base); Word(handle + 28, result.address);
    Word(handle + 36, result.packed_dimensions); Word(handle + 40, result.format);
    Check(guest.Surface(handle).width == width && guest.Surface(handle).height == height, "Surface dimension decoder mismatch");
    return result;
  }
  void Bind(const SurfaceDescriptor& color, const SurfaceDescriptor& depth) {
    Word(arena + 12432, color.handle); Word(arena + 12448, depth.handle);
  }
  Texture MakeTexture(bool depth, bool tiled, xenos::Endian endian) {
    Texture value{}; value.handle = arena + next_header; next_header += 64;
    auto& fetch = value.fetch;
    fetch.type = xenos::FetchConstantType::kTexture;
    fetch.format = depth ? xenos::TextureFormat::k_24_8_FLOAT : xenos::TextureFormat::k_8_8_8_8;
    fetch.dimension = xenos::DataDimension::k2DOrStacked;
    fetch.size_2d.width = fetch.size_2d.height = 15;
    fetch.pitch = 2; fetch.tiled = tiled; fetch.endianness = endian;
    fetch.base_address = (backing + next_backing) >> 12; next_backing += 16384;
    fetch.swizzle = xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA;
    Check(TextureInfo::Prepare(fetch, &value.info), "Texture fetch preparation failed");
    const auto words = std::bit_cast<std::array<uint32_t, 6>>(fetch);
    for (size_t i = 0; i < words.size(); ++i) Word(value.handle + 28 + uint32_t(i * sizeof(uint32_t)), words[i]);
    auto storage = guest.Writable(fetch.base_address << 12, 16384, true);
    Check(!storage.empty(), "Texture backing is not writable"); std::memset(storage.data(), 0xCD, storage.size());
    return value;
  }
  void Clear(uint32_t flags, const std::array<float, 4>& color, double depth, uint32_t stencil,
             ResolveRectangle rectangle = {0, 0, 16, 16}) {
    ClearCommand command{}; command.device = arena; command.flags = flags;
    command.left = rectangle.left; command.top = rectangle.top; command.right = rectangle.right; command.bottom = rectangle.bottom;
    for (size_t i = 0; i < color.size(); ++i) command.color_bits[i] = std::bit_cast<uint32_t>(color[i]);
    command.depth_bits = std::bit_cast<uint64_t>(depth); command.stencil = stencil;
    Submit(command);
  }
  ResolveCommand ResolveRequest(const SurfaceDescriptor& source, const Texture& destination, bool depth) {
    ResolveCommand command{}; command.device = arena; command.source = source;
    command.flags = depth ? 0x14u : NormalizeResolveSampleFlags(0, source.sample_type);
    command.destination_texture = destination.handle;
    std::memcpy(command.destination_fetch, &destination.fetch, sizeof(destination.fetch));
    return command;
  }
  std::vector<uint32_t> Read(const Texture& texture) {
    TextureLockCommand command{}; command.texture = texture.handle;
    TextureLockResult result{};
    Check(renderer.Execute(Bytes(command), std::as_writable_bytes(std::span(&result, 1)), error), "Readback: " + error);
    Check(result.copied_to_guest == 1 && result.generation != 0, "Readback did not publish a generation");
    std::vector<uint32_t> pixels(16u * 16u);
    uint32_t px = 0, py = 0;
    const auto address = texture.info.GetMipLocation(0, &px, &py, true);
    const auto extent = texture.info.GetMipExtent(0, true);
    auto bytes = guest.Read(address, 16384, true);
    for (uint32_t y = 0; y < 16; ++y) for (uint32_t x = 0; x < 16; ++x) {
      const size_t offset = texture.info.is_tiled ? size_t(texture_util::GetTiledOffset2D(px + x, py + y, extent.block_pitch_h, 2)) :
          (size_t(py + y) * extent.block_pitch_h + px + x) * sizeof(uint32_t);
      texture_conversion::CopySwapBlock(texture.info.endianness, &pixels[size_t(y) * 16 + x], bytes.data() + offset, sizeof(uint32_t));
    }
    return pixels;
  }
};

// A load-action clear produces an attachment aspect even when the guest's
// explicit clear only named the other aspect. Exercise both full and partial
// first clears, then read the packed depth/stencil snapshot back to guest bytes.
void ExerciseInitialization(Fixture& f, uint32_t sample_type, bool tiled) {
  for (uint32_t flags : {0x10u, 0x20u}) for (bool partial : {false, true}) {
    const auto surface = f.Surface(true, sample_type);
    f.Bind({}, surface);
    const ResolveRectangle rectangle = partial ? ResolveRectangle{4, 4, 12, 12}
                                               : ResolveRectangle{0, 0, 16, 16};
    f.Clear(flags, {}, 0.5, 0x63, rectangle);
    const auto texture = f.MakeTexture(true, tiled, xenos::Endian::k8in32);
    f.Submit(f.ResolveRequest(surface, texture, true));
    const auto pixels = f.Read(texture);
    for (size_t y = 0; y < 16; ++y) for (size_t x = 0; x < 16; ++x) {
      const bool touched = !partial || (x >= 4 && x < 12 && y >= 4 && y < 12);
      const float depth = touched && flags == 0x10u ? 0.5f : 0.0f;
      const uint32_t stencil = touched && flags == 0x20u ? 0x63u : 0u;
      Check(pixels[y * 16 + x] == ((xenos::Float32To20e4(depth, false) << 8) | stencil),
            "An implicit load clear was lost or the untouched aspect was overwritten");
    }
    // A depth-only handoff also initializes previously undefined stencil to zero.
    const auto forward = f.Surface(true, 0);
    DepthSurfaceHandoffCommand handoff{};
    handoff.device = f.arena; handoff.source = surface; handoff.destination = forward;
    handoff.source_texture = texture.handle;
    f.Submit(handoff);
    const auto forwarded = f.MakeTexture(true, tiled, xenos::Endian::k8in32);
    f.Submit(f.ResolveRequest(forward, forwarded, true));
    const auto result = f.Read(forwarded);
    for (size_t i = 0; i < result.size(); ++i)
      Check(result[i] == (pixels[i] & 0xFFFFFF00u), "First depth handoff did not initialize stencil");
  }
}

// The retail postfx resolve reads a multisampled view of an earlier, larger
// single-sample surface. A matching address alone is not a valid relationship.
void ExerciseColorViews(Fixture& f, bool tiled) {
  for (uint32_t requested_type : {1u, 2u}) {
    const uint32_t width = 16u * GuestSampleScaleX(xenos::MsaaSamples(requested_type));
    const uint32_t height = 16u * GuestSampleScaleY(xenos::MsaaSamples(requested_type));
    auto owner = f.Surface(false, 0, width, height);
    auto requested = f.Surface(false, requested_type);
    requested.address = (owner.address & 0x7FFu) | 0x00030000u;
    f.Word(requested.handle + 28, requested.address);
    GuestSurfaceView a{}, b{};
    Check(DecodeGuestSurfaceView(owner, false, a) && DecodeGuestSurfaceView(requested, false, b) &&
        GetGuestPlacementKey(a) == GetGuestPlacementKey(b), "Invalid surface-view fixture");
    f.Bind(owner, {});
    f.Clear(1, {0, 0, 0, 1}, 0, 0, {0, 0, int32_t(width), int32_t(height)});
    for (uint32_t y = 1; y < height; y += 2)
      f.Clear(1, {1, 1, 1, 1}, 0, 0, {0, int32_t(y), int32_t(width), int32_t(y + 1)});
    // A newer write with the same storage address but a different pitch must
    // neither replace the valid view nor supply content after its release.
    auto unrelated = f.Surface(false, 0, width, height);
    unrelated.address = requested.address;
    unrelated.base = width * 2;
    f.Word(unrelated.handle + 24, unrelated.base);
    f.Word(unrelated.handle + 28, unrelated.address);
    f.Bind(unrelated, {});
    f.Clear(1, {0, 1, 0, 1}, 0, 0, {0, 0, int32_t(width), int32_t(height)});
    f.Bind(requested, {});
    const auto destination = f.MakeTexture(false, tiled, xenos::Endian::k8in32);
    auto resolve = f.ResolveRequest(requested, destination, false);
    f.Submit(resolve);
    for (uint32_t pixel : f.Read(destination))
      Check(pixel == 0xFF808080u, "Multisample view did not average its owning image's samples");
    resolve.flags = 0x10u;
    f.Submit(resolve);
    for (uint32_t pixel : f.Read(destination))
      Check(pixel == 0xFF000000u, "Selected sample zero was not preserved across views");
    resolve.flags = 0x20u;
    f.Submit(resolve);
    for (uint32_t pixel : f.Read(destination))
      Check(pixel == 0xFFFFFFFFu, "Selected sample one was not preserved across views");
    ReleaseResourceCommand release{}; release.resource = owner.handle; f.Submit(release);
    Check(!f.renderer.Submit(Bytes(resolve), f.error),
          "Released source or unrelated equal-address view supplied resolve content");
  }
  std::printf("color_view_resolves tiled=%u passed=true\n", unsigned(tiled));
}

void ExerciseReflectionInitialization(Fixture& f, bool tiled) {
  const auto mirror = f.Surface(false, 0);
  const auto texture = f.MakeTexture(false, tiled, xenos::Endian::k8in32);
  RegisterReflectionTargetCommand registration{};
  registration.surface = mirror.handle; registration.texture = texture.handle;
  registration.wrapper = mirror.handle;
  registration.logical_width = registration.logical_height = 16;
  registration.physical_width = registration.physical_height = 16;
  f.Submit(registration);
  auto unrelated = f.Surface(false, 0);
  unrelated.address = mirror.address; f.Word(unrelated.handle + 28, unrelated.address);
  f.Bind(unrelated, {}); f.Clear(1, {0, 0, 1, 1}, 0, 0);
  f.Bind(mirror, {});
  const auto resolve = f.ResolveRequest(mirror, texture, false);
  f.Submit(resolve);
  for (uint32_t pixel : f.Read(texture))
    Check(pixel == 0, "Uncaptured mirror was undefined or borrowed unrelated content");
  f.Submit(resolve);
  for (uint32_t pixel : f.Read(texture))
    Check(pixel == 0, "Repeated inactive mirror resolve changed its initialized storage");
  f.Clear(1, {1, 0, 0, 1}, 0, 0); f.Submit(resolve);
  for (uint32_t pixel : f.Read(texture))
    Check(pixel == 0xFF0000FFu, "First real mirror capture did not replace its fallback");
  std::printf("reflection_initialization tiled=%u passed=true\n", unsigned(tiled));
}

void Exercise(Fixture& f, uint32_t sample_type, bool tiled) {
  const auto color = f.Surface(false, sample_type), depth = f.Surface(true, sample_type);
  f.Bind(color, depth);
  auto color_texture = f.MakeTexture(false, tiled, xenos::Endian::k8in32);
  auto depth_texture = f.MakeTexture(true, tiled, xenos::Endian::k8in32);
  f.Clear(0x31, {1, 0, 0, 1}, 0, 0x35);
  f.Clear(0x31, {0, 1, 0, 1}, 0.5, 0xA3, {4, 4, 12, 12});
  auto color_resolve = f.ResolveRequest(color, color_texture, false);
  auto depth_resolve = f.ResolveRequest(depth, depth_texture, true);
  f.Submit(color_resolve); f.Submit(depth_resolve);
  auto colors = f.Read(color_texture), depths = f.Read(depth_texture);
  for (size_t y = 0; y < 16; ++y) for (size_t x = 0; x < 16; ++x) {
    const bool inner = x >= 4 && x < 12 && y >= 4 && y < 12;
    Check(colors[y * 16 + x] == (inner ? 0xFF00FF00u : 0xFF0000FFu), "Partial color clear changed the wrong pixel");
    const uint32_t expected = (xenos::Float32To20e4(inner ? 0.5f : 0.0f, false) << 8) | (inner ? 0xA3u : 0x35u);
    Check(depths[y * 16 + x] == expected, "Depth/stencil resolve or readback mismatch");
  }
  // A partial resolve must preserve previously produced pixels outside its rectangle.
  f.Clear(1, {0, 0, 1, 1}, 0, 0);
  color_resolve.source_rectangle_valid = 1; color_resolve.source_rectangle = {0, 0, 4, 4};
  color_resolve.destination_point_valid = 1; color_resolve.destination_point = {6, 6};
  f.Submit(color_resolve);
  auto partial = f.Read(color_texture);
  for (size_t y = 0; y < 16; ++y) for (size_t x = 0; x < 16; ++x)
    Check(partial[y * 16 + x] == (x >= 6 && x < 10 && y >= 6 && y < 10 ? 0xFFFF0000u : colors[y * 16 + x]),
          "Partial resolve failed to preserve destination contents");
  // Mutating the source attachment must not change its earlier resolved snapshot.
  f.Clear(0x30, {}, 1, 0xFF);
  const auto forward = f.Surface(true, 0);
  f.Bind({}, forward); f.Clear(0x30, {}, 0.25, 0x17);
  DepthSurfaceHandoffCommand handoff{};
  handoff.device = f.arena; handoff.source = depth; handoff.destination = forward; handoff.source_texture = depth_texture.handle;
  f.Submit(handoff);
  auto forward_texture = f.MakeTexture(true, tiled, xenos::Endian::k8in32);
  auto forward_resolve = f.ResolveRequest(forward, forward_texture, true); f.Submit(forward_resolve);
  auto preserved = f.Read(forward_texture);
  for (size_t i = 0; i < preserved.size(); ++i)
    Check(preserved[i] == ((depths[i] & 0xFFFFFF00u) | 0x17u), "Depth-only handoff changed destination stencil");
  handoff.stencil_policy = ForwardStencilHandoffPolicy::kRebuildSceneCoverage; f.Submit(handoff); f.Submit(forward_resolve);
  auto rebuilt = f.Read(forward_texture);
  for (size_t i = 0; i < rebuilt.size(); ++i)
    Check(rebuilt[i] == ((depths[i] & 0xFFFFFF00u) | ((depths[i] >> 8) ? kForwardCoveredSceneStencil : kForwardEmptySceneStencil)),
          "Scene handoff did not rebuild packed-depth coverage");
  auto alias = f.MakeTexture(false, tiled, xenos::Endian::k8in32);
  alias.fetch.swizzle = 0x60A;
  const auto alias_words = std::bit_cast<std::array<uint32_t, 6>>(alias.fetch);
  for (size_t i = 0; i < alias_words.size(); ++i) f.Word(alias.handle + 28 + uint32_t(i * sizeof(uint32_t)), alias_words[i]);
  RegisterVirtualResourceCommand registration{};
  registration.resource = alias.handle; registration.kind = VirtualResourceKind::kTexture;
  registration.guest_backing_width = registration.logical_width = registration.physical_width = 16;
  registration.guest_backing_height = registration.logical_height = registration.physical_height = 16;
  registration.packed_depth_source = depth_texture.handle;
  f.Submit(registration);
  auto aliases = f.Read(alias);
  for (size_t i = 0; i < aliases.size(); ++i) {
    uint32_t expected = 0;
    for (uint32_t c = 0; c < 4; ++c) {
      const uint32_t component = (alias.fetch.swizzle >> (c * 3)) & 7u;
      const uint32_t value = component < 4 ? (depths[i] >> (component * 8)) & 255u : (component & 1u) * 255u;
      expected |= value << (c * 8);
    }
    Check(aliases[i] == expected, "Packed depth alias byte order/swizzle mismatch");
  }
  TextureLockCommand invalid{}; invalid.texture = alias.handle; invalid.level = 32;
  TextureLockResult result{};
  Check(!f.renderer.Execute(Bytes(invalid), std::as_writable_bytes(std::span(&result, 1)), f.error), "Invalid readback level was accepted");
  Check(f.renderer.Finish(f.error), f.error);
  std::printf("title_passes samples=%u tiled=%u passed=true\n", 1u << sample_type, unsigned(tiled));
}

#include "metal_resolve_reuse_cases.inc"
#include "metal_geometry_cases.inc"
#include "metal_constant_tracking_cases.inc"
#include "metal_font_draw_cases.inc"
#include "metal_hybrid_aa_cases.inc"
#include "metal_optimization_cases.inc"
#include "metal_resolve_clear_dependency_cases.inc"
}

int main(int argc, char** argv) {
  @autoreleasepool {
    try {
      Check(argc == 3 || argc == 4, "usage: rex-metal-renderer-test shader-directory passes.metallib [geometry-fixture-directory]");
      InitLogging(); memory::Memory memory; Check(memory.Initialize(), "Guest address reservation failed");
      std::string error; auto context = ui::metal::MetalContext::Create(error); Check(bool(context), error);
      NSError* native_error = nil;
      context->pass_library = [context->device newLibraryWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[2]]] error:&native_error];
      Check(context->pass_library != nil, ui::metal::MetalError(native_error, "Pass library load failed"));
      {
        ShaderCache overrides(context, "override_shader_archive");
        const auto archive = (std::filesystem::path(argv[1]) / "override_shader_archive.bin").string();
        Check(overrides.InitializeFile(archive, error), "Motion blur archive: " + error);
        constexpr uint64_t kMotionBlurHash = 0xEE75C9F6AA1AB16Aull;
        const auto metadata = overrides.Lookup(kMotionBlurHash, ShaderStage::kPixel, error);
        Check(metadata && metadata->hash == kMotionBlurHash, "Motion blur Metal metadata unavailable: " + error);
        Check(overrides.Function(kMotionBlurHash, ShaderStage::kPixel, 0, false, error) != nil,
              "Motion blur Metal function unavailable: " + error);
        std::printf("metal_motion_blur_override=passed hash=%016llX\n",
                    static_cast<unsigned long long>(kMotionBlurHash));
      }
      for (uint32_t samples = 0; samples < 3; ++samples) for (bool tiled : {false, true}) {
        Check(cvar::SetFlagByName("gta4_native_anti_aliasing", samples == 0 ? "off" : samples == 1 ? "msaa2x" : "msaa4x"), "AA test selection failed");
        Fixture fixture(memory, context, argv[1]); ExerciseInitialization(fixture, samples, tiled); Exercise(fixture, samples, tiled); ExerciseResolveReuse(fixture, samples, tiled);
      }
      for (bool tiled : {false, true}) {
        Fixture fixture(memory, context, argv[1]); ExerciseColorViews(fixture, tiled);
        ExerciseReflectionInitialization(fixture, tiled);
      }
      {
        Check(cvar::SetFlagByName("gta4_native_anti_aliasing", "off"), "Font AA selection failed");
        Fixture font_fixture(memory, context, argv[1]); ExerciseFontDraws(font_fixture);
        QueryDeviceCapabilitiesCommand query{}; DeviceCapabilitiesResult capabilities{};
        Check(font_fixture.renderer.Execute(Bytes(query), std::as_writable_bytes(std::span(&capabilities,1)), error), error);
        Check(capabilities.max_image_dimension_2d >= 4096 &&
              (capabilities.capabilities & kCapabilityPerceptualPresentationBeforeHdr),
              "Metal did not advertise its pre-HDR spatial upscaling contract");
      }
      if (argc == 4) {
        Check(cvar::SetFlagByName("gta4_native_anti_aliasing", "off"), "Geometry AA selection failed");
        Check(cvar::SetFlagByName("gta4_metal_audit_guest_constants", "true"), "Constant audit selection failed");
        { Fixture fixture(memory, context, argv[3]); ExerciseConstantTracking(fixture); }
        for (bool scaled : {false, true}) {
          Fixture fixture(memory, context, argv[3]); ExerciseGeometryDraws(fixture, scaled);
        }
        ExerciseOptimizations(memory,context,argv[3]);
        ExerciseResolveClearDependencies(memory,context,argv[3]);
        ExerciseHybridAA(memory, context, argv[3]);
      }
      std::printf("metal_title_passes=passed checks=%zu device=%s\n", checks, context->device.name.UTF8String);
      return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "Metal renderer test: %s\n", error.what()); return 1; }
  }
}
