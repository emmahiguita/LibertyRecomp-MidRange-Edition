#pragma once
// Experimental: mark the floating-point arithmetic of a fragment shader
// RelaxedPrecision so a driver that lowers mediump can run it at 16 bits
// (Adreno executes FP16 at twice the FP32 rate). Not exact: 10-bit mantissas.
#include <cstdint>
#include <optional>
#include <unordered_set>
#include <vector>

namespace rex::graphics::gta4_native {

struct SpirvRelaxedResult {
  std::vector<uint32_t> words;
  uint32_t decorated = 0;
};

inline std::optional<SpirvRelaxedResult> MarkSpirvArithmeticRelaxed(const std::vector<uint32_t>& in) {
  constexpr uint16_t kOpTypeFloat = 22, kOpTypeVector = 23, kOpDecorate = 71, kOpExtInst = 12,
                     kOpFunction = 54;
  constexpr uint32_t kRelaxedPrecision = 0;
  if (in.size() < 5 || in[0] != 0x07230203u) return std::nullopt;
  std::unordered_set<uint32_t> float_types;
  size_t first_type = 0;
  std::vector<uint32_t> targets;
  const auto arithmetic = [](uint16_t op) {
    // FNegate..FMod, VectorTimesScalar, Dot, and ExtInst (GLSL.std.450 math).
    return (op >= 127 && op <= 141) || op == 142 || op == 148 || op == kOpExtInst;
  };
  for (size_t i = 5; i < in.size();) {
    const uint32_t count = in[i] >> 16;
    const uint16_t op = uint16_t(in[i] & 0xFFFF);
    if (!count || i + count > in.size()) return std::nullopt;
    const uint32_t* w = in.data() + i;
    if (op >= 19 && op <= 39 && !first_type) first_type = i;  // first OpType*
    if (op == kOpTypeFloat && w[2] == 32) float_types.insert(w[1]);
    if (op == kOpTypeVector && float_types.count(w[2])) float_types.insert(w[1]);
    if (count >= 3 && arithmetic(op) && float_types.count(w[1])) targets.push_back(w[2]);
    i += count;
  }
  if (targets.empty() || !first_type) return std::nullopt;
  SpirvRelaxedResult result;
  result.words.reserve(in.size() + targets.size() * 3);
  result.words.insert(result.words.end(), in.begin(), in.begin() + first_type);
  for (uint32_t id : targets) {
    result.words.push_back(uint32_t(3) << 16 | kOpDecorate);
    result.words.push_back(id);
    result.words.push_back(kRelaxedPrecision);
  }
  result.words.insert(result.words.end(), in.begin() + first_type, in.end());
  result.decorated = uint32_t(targets.size());
  (void)kOpFunction;
  return result;
}

}  // namespace rex::graphics::gta4_native
