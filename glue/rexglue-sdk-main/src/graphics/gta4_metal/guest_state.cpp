#include <rex/graphics/gta4_native/temporal_commands.h>
#include "guest_state.h"
#include <bit>
#include <cstring>
#include "../gta4_native/native_fixed_function_policy.h"
#include "../gta4_native/native_clip_control.h"
#include "../gta4_native/alpha_to_coverage_util.h"
namespace rex::graphics::gta4_metal {
using namespace gta4_native;

uint32_t GuestWord(std::span<const uint8_t> bytes, size_t offset) {
  if (offset > bytes.size() || sizeof(uint32_t) > bytes.size() - offset) return 0;
  uint32_t value;
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return __builtin_bswap32(value);
}
FixedState DecodeFixedState(std::span<const uint8_t> device_snapshot) {
  if (device_snapshot.size() < gta4_native::kGuestDeviceSize) return {};
  return gta4_native::core::DecodeFixedState(device_snapshot);
}

uint32_t VertexSemanticLocation(uint8_t usage, uint8_t usage_index) {
  struct UsageLocation {
    uint8_t usage;
    uint8_t usage_index;
    uint32_t location;
  };
  static constexpr UsageLocation kUsageLocations[] = {
      {0, 0, 0},   {9, 0, 0},   {0, 1, 1},   {0, 2, 2},   {0, 3, 3},   {3, 0, 4},   {3, 1, 5},
      {3, 2, 6},   {3, 3, 7},   {6, 0, 8},   {6, 1, 9},   {6, 2, 10},  {6, 3, 11},  {7, 0, 12},
      {5, 0, 13},  {5, 1, 14},  {5, 2, 15},  {5, 3, 16},  {10, 0, 17}, {2, 0, 18},  {1, 0, 19},
      {5, 4, 20},  {5, 5, 21},  {5, 6, 22},  {5, 7, 23},  {5, 8, 24},  {5, 9, 25},  {5, 10, 26},
      {5, 11, 27}, {5, 12, 28}, {5, 13, 29}, {5, 14, 30}, {5, 15, 31}, {10, 1, 32}, {3, 4, 33},
      {3, 5, 34},  {6, 4, 35},  {6, 5, 36},  {7, 1, 37},  {7, 2, 38},  {7, 3, 39},
  };
  for (const UsageLocation& candidate : kUsageLocations) {
    if (candidate.usage == usage && candidate.usage_index == usage_index) {
      return candidate.location;
    }
  }
  return UINT32_MAX;
}

size_t TitleCommandSize(CommandType type) {
  switch (type) {
    case CommandType::kDeviceCreated:
    case CommandType::kDeviceDestroyed:
      return sizeof(DeviceCommand);
    case CommandType::kRegisterShader:
      return sizeof(RegisterShaderCommand);
    case CommandType::kRegisterVertexDeclaration:
      return sizeof(RegisterVertexDeclarationCommand);
    case CommandType::kSetRenderState:
      return sizeof(SetRenderStateCommand);
    case CommandType::kSetPixelShader:
    case CommandType::kSetVertexShader:
      return sizeof(SetShaderCommand);
    case CommandType::kSetVertexDeclaration:
      return sizeof(SetVertexDeclarationCommand);
    case CommandType::kSetTexture:
      return sizeof(SetTextureCommand);
    case CommandType::kResourceUnlock:
      return sizeof(ResourceUnlockCommand);
    case CommandType::kSetDepthStencil:
      return sizeof(SetDepthStencilCommand);
    case CommandType::kSetRenderTarget:
      return sizeof(SetRenderTargetCommand);
    case CommandType::kSetVertexStream:
      return sizeof(SetVertexStreamCommand);
    case CommandType::kSetIndexBuffer:
      return sizeof(SetIndexBufferCommand);
    case CommandType::kDrawPrimitive:
      return sizeof(DrawPrimitiveCommand);
    case CommandType::kDrawPrimitiveUp:
      return sizeof(DrawPrimitiveUpCommand);
    case CommandType::kDrawIndexedPrimitive:
      return sizeof(DrawIndexedPrimitiveCommand);
    case CommandType::kResolve:
      return sizeof(ResolveCommand);
    case CommandType::kTextureLock:
      return sizeof(TextureLockCommand);
    case CommandType::kClear:
      return sizeof(ClearCommand);
    case CommandType::kRenderPhaseMarker:
      return sizeof(RenderPhaseMarkerCommand);
    case CommandType::kPresent:
      return sizeof(PresentCommand);
    case CommandType::kQueryDeviceCapabilities:
      return sizeof(QueryDeviceCapabilitiesCommand);
    case CommandType::kRegisterReflectionTarget:
      return sizeof(RegisterReflectionTargetCommand);
    case CommandType::kReleaseResource:
      return sizeof(ReleaseResourceCommand);
    case CommandType::kUpdateEnvironmentalData:
      return sizeof(UpdateEnvironmentalDataCommand);
    case CommandType::kDepthSurfaceHandoff:
      return sizeof(DepthSurfaceHandoffCommand);
    case CommandType::kRegisterVirtualResource:
      return sizeof(RegisterVirtualResourceCommand);
    case CommandType::kTemporalUpdate:
      return sizeof(TemporalCommand);
  }
  return 0;
}
SurfaceDescriptor DecodeSurface(uint32_t handle, std::span<const uint8_t> bytes) {
  SurfaceDescriptor out{};
  if (!handle || bytes.size() < 44) return out;
  out.handle = handle;
  out.flags = GuestWord(bytes, 0);
  out.base = GuestWord(bytes, 24);
  out.address = GuestWord(bytes, 28);
  out.packed_dimensions = GuestWord(bytes, 36);
  out.format = GuestWord(bytes, 40);
  out.width = (std::rotl(out.packed_dimensions, 14) & 0x3FFF) + 1;
  out.height = (std::rotl(out.packed_dimensions, 29) & 0x7FFF) + 1;
  out.sample_type = (out.base >> 16) & 3u;
  return out;
}
} // namespace rex::graphics::gta4_metal
