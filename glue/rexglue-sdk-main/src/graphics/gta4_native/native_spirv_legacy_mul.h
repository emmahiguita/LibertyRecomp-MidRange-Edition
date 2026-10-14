#pragma once
// Cheaper, bit-exact Xenos "legacy" multiplies in recompiled shaders.
//
// XenosRecomp emits every MUL/MAD product with Xenos semantics: if either
// factor has a zero exponent (zero or denormal) the product is +0, so 0 * inf
// and 0 * NaN give 0. Per component that is
//   ua = bits(a); ub = bits(b); p = a * b;
//   za = (ua & 0x7F800000) == 0 ? 1 : 0;  zb = (ub & 0x7F800000) == 0 ? 1 : 0;
//   r  = bits_as_float((za | zb) != 0 ? 0 : bits(p));
// about ten ALU operations around one multiply. A zero exponent is exactly
// |x| < FLT_MIN (NaN and infinity have the maximum exponent and compare
// false), so the same result is
//   r = (|a| < FLT_MIN || |b| < FLT_MIN) ? +0.0 : a * b
// where the absolute values are free source modifiers on the GPU. The final
// bitcast of each matched expression is replaced; the old integer chain is left
// dead for the driver to remove.
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rex::graphics::gta4_native {

struct SpirvLegacyMulResult {
  std::vector<uint32_t> words;
  uint32_t rewritten = 0;
};

// ieee: drop the zero-preserving rule and use the plain IEEE product (one
// operation; 0 * inf and 0 * NaN become NaN instead of 0). Not bit-exact.
inline std::optional<SpirvLegacyMulResult> SimplifySpirvLegacyMultiplies(
    const std::vector<uint32_t>& in, bool ieee = false) {
  constexpr uint16_t kOpExtInstImport = 11, kOpExtInst = 12, kOpMemoryModel = 14,
                     kOpTypeBool = 20, kOpTypeFloat = 22, kOpTypeVector = 23, kOpConstant = 43,
                     kOpConstantComposite = 44, kOpConstantNull = 46, kOpFunction = 54,
                     kOpBitcast = 124, kOpFMul = 133, kOpLogicalOr = 166, kOpSelect = 169,
                     kOpIEqual = 170, kOpINotEqual = 171, kOpFOrdLessThan = 184,
                     kOpBitwiseOr = 197, kOpBitwiseAnd = 199, kOpCopyObject = 83;
  constexpr uint32_t kGlslFAbs = 4;
  constexpr uint32_t kExponentMask = 0x7F800000u, kFltMinBits = 0x00800000u;
  if (in.size() < 5 || in[0] != 0x07230203u) return std::nullopt;

  uint32_t bound = in[3];
  std::unordered_map<uint32_t, size_t> def;  // result id -> word index
  std::unordered_map<uint32_t, uint32_t> vector_count;  // float vector type -> components
  uint32_t float_type = 0, glsl = 0;
  size_t first_function = 0, memory_model = 0;
  // Result-id position per opcode (type-defining ops carry the id in word 1).
  const auto result_index = [](uint16_t op) -> int {
    switch (op) {
      case kOpExtInstImport: case kOpTypeBool: case kOpTypeFloat: case kOpTypeVector:
      case 21 /*OpTypeInt*/: case 19 /*OpTypeVoid*/: case 32 /*OpTypePointer*/:
      case 33 /*OpTypeFunction*/: case 248 /*OpLabel*/:
        return 1;
      default:
        return 2;
    }
  };
  for (size_t i = 5; i < in.size();) {
    const uint32_t count = in[i] >> 16;
    const uint16_t op = uint16_t(in[i] & 0xFFFF);
    if (!count || i + count > in.size()) return std::nullopt;
    const uint32_t* w = in.data() + i;
    if (op == kOpMemoryModel) memory_model = i;
    if (op == kOpFunction && !first_function) first_function = i;
    if (op == kOpTypeFloat && w[2] == 32 && !float_type) float_type = w[1];
    if (op == kOpExtInstImport &&
        std::string_view(reinterpret_cast<const char*>(w + 2)).starts_with("GLSL.std.450"))
      glsl = w[1];
    if (count >= 3 && (op == kOpBitcast || op == kOpFMul || op == kOpSelect || op == kOpIEqual ||
                       op == kOpINotEqual || op == kOpBitwiseOr || op == kOpBitwiseAnd ||
                       op == kOpConstant || op == kOpConstantComposite ||
                       op == kOpConstantNull)) {
      def[w[2]] = i;
    } else if (count >= 2 && result_index(op) == 1 &&
               (op == kOpTypeVector || op == kOpTypeBool)) {
      def[w[1]] = i;
    }
    i += count;
  }
  if (!float_type || !first_function) return std::nullopt;
  for (size_t i = 5; i < first_function;) {
    const uint32_t count = in[i] >> 16;
    const uint32_t* w = in.data() + i;
    if (uint16_t(w[0] & 0xFFFF) == kOpTypeVector && w[2] == float_type) vector_count[w[1]] = w[3];
    i += count;
  }

  const auto inst = [&](uint32_t id, uint16_t op) -> const uint32_t* {
    const auto found = def.find(id);
    if (found == def.end()) return nullptr;
    const uint32_t* w = in.data() + found->second;
    return uint16_t(w[0] & 0xFFFF) == op ? w : nullptr;
  };
  // Scalar constant value, or the common value of a splatted composite.
  const auto constant_value = [&](uint32_t id) -> std::optional<uint32_t> {
    const auto found = def.find(id);
    if (found == def.end()) return std::nullopt;
    const uint32_t* w = in.data() + found->second;
    const uint16_t op = uint16_t(w[0] & 0xFFFF);
    const uint32_t count = w[0] >> 16;
    if (op == kOpConstant && count == 4) return w[3];
    if (op == kOpConstantNull) return 0u;
    if (op == kOpConstantComposite && count >= 4) {
      std::optional<uint32_t> value;
      for (uint32_t k = 3; k < count; ++k) {
        const auto part = inst(w[k], kOpConstant);
        if (!part || (part[0] >> 16) != 4 || (value && *value != part[3])) return std::nullopt;
        value = part[3];
      }
      return value;
    }
    return std::nullopt;
  };
  // ((bits(x) & 0x7F800000) == 0) ? 1 : 0, returning x.
  const auto zero_exponent_test = [&](uint32_t id) -> std::optional<uint32_t> {
    const uint32_t* select = inst(id, kOpSelect);
    if (!select || (select[0] >> 16) != 6) return std::nullopt;
    if (constant_value(select[4]) != 1u || constant_value(select[5]) != 0u) return std::nullopt;
    const uint32_t* equal = inst(select[3], kOpIEqual);
    if (!equal || constant_value(equal[4]) != 0u) return std::nullopt;
    const uint32_t* masked = inst(equal[3], kOpBitwiseAnd);
    if (!masked) return std::nullopt;
    uint32_t source = 0;
    if (constant_value(masked[4]) == kExponentMask) source = masked[3];
    else if (constant_value(masked[3]) == kExponentMask) source = masked[4];
    else return std::nullopt;
    const uint32_t* cast = inst(source, kOpBitcast);
    if (!cast) return std::nullopt;
    return cast[3];
  };

  struct Match {
    uint32_t type, result, a, b, product, bool_type;
  };
  std::unordered_map<size_t, Match> matches;  // word index of the final OpBitcast
  for (size_t i = 5; i < in.size();) {
    const uint32_t count = in[i] >> 16;
    const uint32_t* w = in.data() + i;
    if (uint16_t(w[0] & 0xFFFF) == kOpBitcast && count == 4 &&
        (w[1] == float_type || vector_count.count(w[1]))) {
      const uint32_t* select = inst(w[3], kOpSelect);
      if (select && (select[0] >> 16) == 6 && constant_value(select[4]) == 0u) {
        const uint32_t* product_bits = inst(select[5], kOpBitcast);
        const uint32_t* product = product_bits ? inst(product_bits[3], kOpFMul) : nullptr;
        const uint32_t* not_equal = inst(select[3], kOpINotEqual);
        const uint32_t* either =
            not_equal && constant_value(not_equal[4]) == 0u ? inst(not_equal[3], kOpBitwiseOr)
                                                            : nullptr;
        if (product && product[1] == w[1] && either) {
          const auto first = zero_exponent_test(either[3]);
          const auto second = zero_exponent_test(either[4]);
          const uint32_t a = product[3], b = product[4];
          if (first && second &&
              ((*first == a && *second == b) || (*first == b && *second == a))) {
            matches[i] = {w[1], w[2], a, b, product[2], not_equal[1]};
          }
        }
      }
    }
    i += count;
  }
  if (matches.empty()) return std::nullopt;

  // Constants: FLT_MIN and +0 per float type used.
  std::vector<uint32_t> globals;
  const auto emit_to = [](std::vector<uint32_t>& out, uint16_t op,
                          std::initializer_list<uint32_t> operands) {
    out.push_back(uint32_t(operands.size() + 1) << 16 | op);
    out.insert(out.end(), operands);
  };
  const uint32_t scalar_min = bound++, scalar_zero = bound++;
  emit_to(globals, kOpConstant, {float_type, scalar_min, kFltMinBits});
  emit_to(globals, kOpConstant, {float_type, scalar_zero, 0u});
  std::map<uint32_t, std::pair<uint32_t, uint32_t>> splats;  // type -> (min, zero)
  splats[float_type] = {scalar_min, scalar_zero};
  for (const auto& [index, match] : matches) {
    if (splats.count(match.type)) continue;
    const uint32_t components = vector_count[match.type];
    const uint32_t min_id = bound++, zero_id = bound++;
    std::vector<uint32_t> min_parts(components, scalar_min), zero_parts(components, scalar_zero);
    globals.push_back(uint32_t(3 + components) << 16 | kOpConstantComposite);
    globals.push_back(match.type);
    globals.push_back(min_id);
    globals.insert(globals.end(), min_parts.begin(), min_parts.end());
    globals.push_back(uint32_t(3 + components) << 16 | kOpConstantComposite);
    globals.push_back(match.type);
    globals.push_back(zero_id);
    globals.insert(globals.end(), zero_parts.begin(), zero_parts.end());
    splats[match.type] = {min_id, zero_id};
  }

  SpirvLegacyMulResult result;
  std::vector<uint32_t>& out = result.words;
  out.reserve(in.size() + matches.size() * 24 + globals.size() + 16);
  out.insert(out.end(), in.begin(), in.begin() + 5);
  if (!glsl) glsl = bound++;
  for (size_t i = 5; i < in.size();) {
    const uint32_t count = in[i] >> 16;
    if (i == memory_model && glsl >= in[3]) {
      // "GLSL.std.450" plus its terminator in four words.
      out.insert(out.end(), {uint32_t(6) << 16 | kOpExtInstImport, glsl, 0x4C534C47u,
                             0x6474732Eu, 0x3035342Eu, 0u});
    }
    if (i == first_function) out.insert(out.end(), globals.begin(), globals.end());
    const auto match = matches.find(i);
    if (match == matches.end()) {
      out.insert(out.end(), in.begin() + i, in.begin() + i + count);
    } else {
      const Match& m = match->second;
      if (ieee) {
        emit_to(out, kOpCopyObject, {m.type, m.result, m.product});
        ++result.rewritten;
        i += count;
        continue;
      }
      const auto [min_id, zero_id] = splats[m.type];
      const uint32_t abs_a = bound++, abs_b = bound++, small_a = bound++, small_b = bound++,
                     small = bound++;
      emit_to(out, kOpExtInst, {m.type, abs_a, glsl, kGlslFAbs, m.a});
      emit_to(out, kOpExtInst, {m.type, abs_b, glsl, kGlslFAbs, m.b});
      emit_to(out, kOpFOrdLessThan, {m.bool_type, small_a, abs_a, min_id});
      emit_to(out, kOpFOrdLessThan, {m.bool_type, small_b, abs_b, min_id});
      emit_to(out, kOpLogicalOr, {m.bool_type, small, small_a, small_b});
      emit_to(out, kOpSelect, {m.type, m.result, small, zero_id, m.product});
      ++result.rewritten;
    }
    i += count;
  }
  out[3] = bound;

  // Drop the now-unused integer chains (pure ops with no remaining use) so the
  // driver parses less. Any word after the result counts as a use, which can
  // only keep an instruction alive, never remove a used one.
  const auto pure = [](uint16_t op) {
    return op == kOpBitcast || op == kOpSelect || op == kOpIEqual || op == kOpINotEqual ||
           op == kOpBitwiseOr || op == kOpBitwiseAnd;
  };
  std::unordered_map<uint32_t, uint32_t> uses;
  std::vector<size_t> candidates;
  for (size_t i = 5; i < out.size();) {
    const uint32_t count = out[i] >> 16;
    const uint16_t op = uint16_t(out[i] & 0xFFFF);
    const bool candidate = pure(op) && count >= 4 && i >= first_function;
    if (candidate) candidates.push_back(i);
    for (uint32_t k = candidate ? 3 : 1; k < count; ++k) ++uses[out[i + k]];
    i += count;
  }
  std::vector<bool> dead(out.size(), false);
  for (bool changed = true; changed;) {
    changed = false;
    for (size_t i : candidates) {
      if (dead[i] || uses[out[i + 2]]) continue;
      dead[i] = true;
      changed = true;
      const uint32_t count = out[i] >> 16;
      for (uint32_t k = 3; k < count; ++k) --uses[out[i + k]];
    }
  }
  std::vector<uint32_t> compact;
  compact.reserve(out.size());
  compact.insert(compact.end(), out.begin(), out.begin() + 5);
  for (size_t i = 5; i < out.size();) {
    const uint32_t count = out[i] >> 16;
    if (!dead[i]) compact.insert(compact.end(), out.begin() + i, out.begin() + i + count);
    i += count;
  }
  out = std::move(compact);
  return result;
}

}  // namespace rex::graphics::gta4_native
