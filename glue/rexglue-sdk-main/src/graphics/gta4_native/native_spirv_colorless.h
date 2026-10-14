#pragma once
// Fragment shader variants for pipelines without color attachments (shadow
// maps, depth-only passes). Such a pipeline only needs what can change depth
// or stencil: discards (alpha test, kill), depth and sample-mask writes.
//
// StripSpirvColorOutputs turns every non-builtin Output variable into a
// Private one. The color computation then feeds nothing and the driver drops
// it, and with it the vertex outputs only it consumed; reads of the former
// outputs (an alpha test of the written color) keep working. When the shader
// cannot discard and writes neither depth nor sample mask, the pipeline needs
// no fragment stage at all (AnalyzeSpirvFragmentEffects).
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rex::graphics::gta4_native {

struct SpirvFragmentEffects {
  bool discards = false;      // OpKill, OpTerminateInvocation, OpDemoteToHelperInvocation
  bool writes_depth = false;  // BuiltIn FragDepth / FragStencilRefEXT
  bool writes_sample_mask = false;
};

inline std::optional<SpirvFragmentEffects> AnalyzeSpirvFragmentEffects(
    const std::vector<uint32_t>& in) {
  constexpr uint16_t kOpKill = 252, kOpTerminateInvocation = 4416, kOpDemote = 5380,
                     kOpDecorate = 71;
  constexpr uint32_t kDecorationBuiltIn = 11, kFragDepth = 22, kSampleMask = 20,
                     kFragStencilRef = 5014;
  if (in.size() < 5 || in[0] != 0x07230203u) return std::nullopt;
  SpirvFragmentEffects effects;
  for (size_t i = 5; i < in.size();) {
    const uint32_t count = in[i] >> 16;
    const uint16_t op = uint16_t(in[i] & 0xFFFF);
    if (!count || i + count > in.size()) return std::nullopt;
    const uint32_t* w = in.data() + i;
    if (op == kOpKill || op == kOpTerminateInvocation || op == kOpDemote) effects.discards = true;
    if (op == kOpDecorate && count >= 4 && w[2] == kDecorationBuiltIn) {
      // Input SampleMask also carries this builtin; treat any as a write.
      if (w[3] == kFragDepth || w[3] == kFragStencilRef) effects.writes_depth = true;
      if (w[3] == kSampleMask) effects.writes_sample_mask = true;
    }
    i += count;
  }
  return effects;
}

inline std::optional<std::vector<uint32_t>> StripSpirvColorOutputs(
    const std::vector<uint32_t>& in) {
  constexpr uint16_t kOpName = 5, kOpMemberName = 6, kOpEntryPoint = 15, kOpTypePointer = 32,
                     kOpFunctionParameter = 55, kOpVariable = 59, kOpLoad = 61, kOpStore = 62,
                     kOpAccessChain = 65, kOpInBoundsAccessChain = 66, kOpDecorate = 71;
  constexpr uint32_t kOutput = 3, kPrivate = 6, kDecorationBuiltIn = 11,
                     kDecorationLocation = 30, kDecorationComponent = 31, kDecorationIndex = 32;
  if (in.size() < 5 || in[0] != 0x07230203u) return std::nullopt;
  const uint32_t version = in[1];

  struct PointerType { uint32_t storage, pointee; };
  std::unordered_map<uint32_t, PointerType> pointer_types;
  std::unordered_set<uint32_t> builtin_ids;
  std::unordered_set<uint32_t> converted;  // variables and access chains into them
  std::unordered_set<uint32_t> needed_types;
  for (size_t i = 5; i < in.size();) {
    const uint32_t count = in[i] >> 16;
    const uint16_t op = uint16_t(in[i] & 0xFFFF);
    if (!count || i + count > in.size()) return std::nullopt;
    const uint32_t* w = in.data() + i;
    if (op == kOpTypePointer && count == 4) pointer_types[w[1]] = {w[2], w[3]};
    if (op == kOpDecorate && count >= 3 && w[2] == kDecorationBuiltIn) builtin_ids.insert(w[1]);
    if (op == kOpVariable && count >= 4 && w[3] == kOutput && !builtin_ids.count(w[2])) {
      converted.insert(w[2]);
      needed_types.insert(w[1]);
    }
    if ((op == kOpAccessChain || op == kOpInBoundsAccessChain) && count >= 4 &&
        converted.count(w[3])) {
      converted.insert(w[2]);
      needed_types.insert(w[1]);
    }
    i += count;
  }
  if (converted.empty()) return std::nullopt;
  // Any other instruction that can take a pointer (function arguments,
  // copies, pointer casts, atomics) is not handled: keep the shader as it is.
  // Only those opcodes are checked, since literals elsewhere may equal ids.
  for (size_t i = 5; i < in.size();) {
    const uint32_t count = in[i] >> 16;
    const uint16_t op = uint16_t(in[i] & 0xFFFF);
    const uint32_t* w = in.data() + i;
    const bool pointer_operands =
        op == 57 /*FunctionCall*/ || op == 63 /*CopyMemory*/ || op == 64 /*CopyMemorySized*/ ||
        op == 67 /*PtrAccessChain*/ || op == 68 /*ArrayLength*/ || op == 83 /*CopyObject*/ ||
        op == 117 /*ConvertPtrToU*/ || op == 124 /*Bitcast*/ || op == 169 /*Select*/ ||
        (op >= 227 && op <= 242) /*atomics*/ || op == 245 /*Phi*/ || op == 401 /*PtrEqual*/ ||
        op == 402 /*PtrNotEqual*/ || op == 403 /*PtrDiff*/;
    if (pointer_operands) {
      for (uint32_t k = 1; k < count; ++k)
        if (converted.count(w[k])) return std::nullopt;
    }
    if (op == 12 /*ExtInst*/) {
      for (uint32_t k = 5; k < count; ++k)
        if (converted.count(w[k])) return std::nullopt;
    }
    if (op == kOpFunctionParameter && count >= 3) {
      const auto type = pointer_types.find(w[1]);
      if (type != pointer_types.end() && type->second.storage == kOutput) return std::nullopt;
    }
    i += count;
  }
  uint32_t bound = in[3];
  std::unordered_map<uint32_t, uint32_t> private_types;
  for (uint32_t type : needed_types) {
    const auto found = pointer_types.find(type);
    if (found == pointer_types.end() || found->second.storage != kOutput) return std::nullopt;
    private_types[type] = bound++;
  }

  std::vector<uint32_t> out;
  out.reserve(in.size() + private_types.size() * 4);
  out.insert(out.end(), in.begin(), in.begin() + 5);
  out[3] = bound;
  for (size_t i = 5; i < in.size();) {
    const uint32_t count = in[i] >> 16;
    const uint16_t op = uint16_t(in[i] & 0xFFFF);
    const uint32_t* w = in.data() + i;
    if (op == kOpEntryPoint && version < 0x00010400u) {
      // Before SPIR-V 1.4 the interface lists only Input and Output.
      size_t name_end = 3;
      while (name_end < count && (w[name_end] >> 24) != 0) ++name_end;
      ++name_end;  // word holding the string terminator
      std::vector<uint32_t> entry(w, w + std::min<size_t>(name_end, count));
      for (size_t k = name_end; k < count; ++k)
        if (!converted.count(w[k])) entry.push_back(w[k]);
      entry[0] = uint32_t(entry.size()) << 16 | kOpEntryPoint;
      out.insert(out.end(), entry.begin(), entry.end());
    } else if (op == kOpDecorate && count >= 3 && converted.count(w[1]) &&
               (w[2] == kDecorationLocation || w[2] == kDecorationComponent ||
                w[2] == kDecorationIndex)) {
      // Interface decorations are invalid on Private variables.
    } else if (op == kOpVariable && count >= 4 && converted.count(w[2])) {
      out.insert(out.end(), w, w + count);
      out[out.size() - count + 1] = private_types.at(w[1]);
      out[out.size() - count + 3] = kPrivate;
    } else if ((op == kOpAccessChain || op == kOpInBoundsAccessChain) && count >= 4 &&
               converted.count(w[2])) {
      out.insert(out.end(), w, w + count);
      out[out.size() - count + 1] = private_types.at(w[1]);
    } else {
      out.insert(out.end(), w, w + count);
      if (op == kOpTypePointer && count == 4) {
        if (const auto found = private_types.find(w[1]); found != private_types.end())
          out.insert(out.end(), {uint32_t(4) << 16 | kOpTypePointer, found->second, kPrivate, w[3]});
      }
    }
    i += count;
  }
  return out;
}

}  // namespace rex::graphics::gta4_native
