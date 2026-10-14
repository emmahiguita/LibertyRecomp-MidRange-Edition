#pragma once
// Fragment shader variant with OpExecutionMode EarlyFragmentTests. Used only
// for pipelines whose depth/stencil state writes nothing (depth test without
// depth writes, stencil without writes): a fragment the shader would discard
// then changes neither attachment, so testing before shading renders the same
// image, while hidden fragments are rejected before their shader runs. Without
// it a shader that can discard (alpha test, kill) is depth-tested late and
// every covered fragment is shaded. Shaders that write depth are not varied.
#include <cstdint>
#include <optional>
#include <vector>

namespace rex::graphics::gta4_native {

inline std::optional<std::vector<uint32_t>> AddSpirvEarlyFragmentTests(
    const std::vector<uint32_t>& in) {
  constexpr uint16_t kOpEntryPoint = 15, kOpExecutionMode = 16, kOpDecorate = 71;
  constexpr uint32_t kExecutionModelFragment = 4, kModeEarlyFragmentTests = 9,
                     kDecorationBuiltIn = 11, kBuiltInFragDepth = 22;
  if (in.size() < 5 || in[0] != 0x07230203u) return std::nullopt;
  uint32_t entry = 0;
  size_t insert_at = 0;
  for (size_t i = 5; i < in.size();) {
    const uint32_t count = in[i] >> 16;
    const uint16_t op = uint16_t(in[i] & 0xFFFF);
    if (!count || i + count > in.size()) return std::nullopt;
    const uint32_t* w = in.data() + i;
    if (op == kOpEntryPoint && count >= 3 && w[1] == kExecutionModelFragment && !entry) {
      entry = w[2];
      insert_at = i + count;
    }
    if (op == kOpExecutionMode && count >= 3 && w[1] == entry) {
      if (w[2] == kModeEarlyFragmentTests) return std::nullopt;  // already early
      insert_at = i + count;
    }
    if (op == kOpDecorate && count >= 4 && w[2] == kDecorationBuiltIn && w[3] == kBuiltInFragDepth)
      return std::nullopt;  // writes depth: tests must stay late
    i += count;
  }
  if (!entry) return std::nullopt;
  std::vector<uint32_t> out;
  out.reserve(in.size() + 3);
  out.insert(out.end(), in.begin(), in.begin() + insert_at);
  out.insert(out.end(), {uint32_t(3) << 16 | kOpExecutionMode, entry, kModeEarlyFragmentTests});
  out.insert(out.end(), in.begin() + insert_at, in.end());
  return out;
}

}  // namespace rex::graphics::gta4_native
