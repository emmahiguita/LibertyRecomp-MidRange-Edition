#pragma once
#include <cstdint>
#include <optional>
namespace rex::graphics::gta4_metal::temporal {
struct ExposureContract {
  uint32_t adaptation_stage, exposure_guest_offset, tone_guest_offset;
};
// Recovered from the named title composite uniforms. The scalar approximates
// base HDR visibility; it never replaces the original tone/color tail.
constexpr std::optional<ExposureContract> ExposureForShader(uint64_t hash) {
  switch (hash) {
    case 0xF6AEB9A606561C54ull: return ExposureContract{2, 9344, 9364};
    case 0x069287E9B706AA14ull: return ExposureContract{5, 9344, 9492};
    case 0x2EAEDEB9125948C2ull: return ExposureContract{4, 9344, 9412};
    case 0x535ACDAB8AE84D82ull: return ExposureContract{4, 9344, 9492};
    case 0x53906ADBE74441C6ull: return ExposureContract{5, 9344, 9412};
    case 0x54FABC991DB485C8ull: return ExposureContract{4, 9344, 9412};
    case 0x5D2A71133DA823F7ull: return ExposureContract{4, 9344, 9412};
    case 0x6533D90E6317B046ull: return ExposureContract{4, 9344, 9492};
    case 0x67C1FB770BB55E69ull: return ExposureContract{5, 9344, 9412};
    case 0x7756A65D296806FFull: return ExposureContract{4, 9344, 9412};
    case 0x79044EA1461439CAull: return ExposureContract{5, 9344, 9412};
    case 0x8F2ECB251AE7FE9Aull: return ExposureContract{4, 9344, 9492};
    case 0x9568A3A7BD3CDE18ull: return ExposureContract{4, 9344, 9412};
    case 0x9649029E1999BEA9ull: return ExposureContract{5, 9344, 9412};
    case 0xB44879A571332324ull: return ExposureContract{5, 9344, 9492};
    case 0xC5D3E7806E478A16ull: return ExposureContract{5, 9344, 9492};
    case 0xCC0C2F3146CCC96Eull: return ExposureContract{4, 9344, 9412};
    case 0xE4AF188ACBDA7362ull: return ExposureContract{5, 9344, 9492};
    case 0xE51D9DD95A333D92ull: return ExposureContract{4, 9344, 9492};
    case 0xEE75C9F6AA1AB16Aull: return ExposureContract{4, 9344, 9492};
    case 0xFE2FC0894C018D11ull: return ExposureContract{4, 9344, 9492};
    default: return std::nullopt;
  }
}
}  // namespace rex::graphics::gta4_metal::temporal
