#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rex::graphics::gta4_native::core {
inline constexpr uint32_t kFontAtlasExtent = 2048;
inline constexpr uint32_t kFontMipLevels = 5;
inline constexpr size_t kFontIdentityBytes = 262144;
enum class VectorFontSet : size_t { kGta4, kTlad, kTbogt };
struct VectorFontAtlas { const char* filename = nullptr; std::vector<uint8_t> alpha; };
const char* VectorFontSetName(VectorFontSet set);
VectorFontSet SelectVectorFontSet(size_t atlas_index, uint64_t stock_identity_hash);
// Existing identity table was derived from a 16-byte alignment prefix. Keep
// that explicit wire convention independent of a backend's upload allocator.
uint64_t StockFontIdentity(std::span<const uint8_t> linear_bc3);
// Load only the atlas actually selected; failure leaves the original font usable.
const VectorFontAtlas* FindVectorFontAtlas(VectorFontSet set, size_t atlas_index);
std::vector<std::vector<uint8_t>> FontCoverageMips(std::span<const uint8_t> alpha,
                                                 uint32_t extent, uint32_t levels);
}  // namespace rex::graphics::gta4_native::core
