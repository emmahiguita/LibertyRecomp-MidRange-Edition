#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <rex/graphics/gta4_native/title_commands.h>
#include "../gta4_native/core/draw_state.h"

namespace rex::graphics::gta4_metal {
// Retail device state is decoded from the same fields as the Vulkan reference.
// This file owns no graphics-API objects and does not model GPU memory layouts.
using FixedState = gta4_native::core::FixedFunctionState;

FixedState DecodeFixedState(std::span<const uint8_t> device);
uint32_t GuestWord(std::span<const uint8_t> bytes, size_t offset);
gta4_native::SurfaceDescriptor DecodeSurface(uint32_t handle, std::span<const uint8_t> bytes);
uint32_t VertexSemanticLocation(uint8_t usage, uint8_t index);
size_t TitleCommandSize(gta4_native::CommandType type);
} // namespace rex::graphics::gta4_metal
