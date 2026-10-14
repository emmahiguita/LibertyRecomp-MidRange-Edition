#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>

namespace rex::graphics::gta4_native {

// Upper bound, in bytes, of the guest constant bank a recompiled shader reads.
// XenosRecomp addresses constants as RawBufferLoad(push.<Bank>ShaderConstants
// + offset), with array offsets of the form (C + min(i, T - 1)) * 16. A small
// interval pass over the SPIR-V bounds every such offset; any use of the bank
// address it cannot bound makes that bank kUnbounded. Callers must upload the
// whole bank in that case.
struct SpirvConstantRange {
  static constexpr uint32_t kUnbounded = std::numeric_limits<uint32_t>::max();
  // Indexed by push-constant member: 0 vertex bank, 1 pixel bank, 2 shared.
  std::array<uint32_t, 3> bytes{0, 0, 0};
};

inline SpirvConstantRange AnalyzeSpirvConstantRange(const uint32_t* words, size_t count) {
  SpirvConstantRange result;
  constexpr uint64_t kInf = std::numeric_limits<uint64_t>::max();
  auto add = [](uint64_t a, uint64_t b) { return (a == kInf || b == kInf || a > kInf - b) ? kInf : a + b; };
  auto mul = [](uint64_t a, uint64_t b) {
    if (a == kInf || b == kInf) return kInf;
    if (a && b > kInf / a) return kInf;
    return a * b;
  };
  auto fail_all = [&] { result.bytes = {SpirvConstantRange::kUnbounded, SpirvConstantRange::kUnbounded,
                                        SpirvConstantRange::kUnbounded}; };
  if (!words || count < 5 || words[0] != 0x07230203u) {
    fail_all();
    return result;
  }
  constexpr uint16_t kOpExtInstImport = 11, kOpExtInst = 12, kOpTypeInt = 21, kOpConstant = 43,
                     kOpVariable = 59, kOpLoad = 61, kOpAccessChain = 65, kOpInBoundsAccessChain = 66,
                     kOpUConvert = 113, kOpSConvert = 114, kOpBitcast = 124, kOpIAdd = 128,
                     kOpIMul = 132, kOpShiftLeftLogical = 196, kOpBitwiseAnd = 199, kOpSelect = 169,
                     kOpPhi = 245, kOpConvertUToPtr = 120, kOpName = 5, kOpDecorate = 71,
                     kOpMemberDecorate = 72, kOpMemberName = 6, kOpEntryPoint = 15;
  constexpr uint32_t kStoragePushConstant = 9;
  constexpr uint32_t kGlslUMin = 38, kGlslSMin = 39, kGlslUClamp = 44, kGlslSClamp = 45;

  std::unordered_map<uint32_t, uint64_t> bound;        // integer value -> upper bound
  std::unordered_map<uint32_t, uint32_t> int_types;    // type id -> width
  std::unordered_map<uint32_t, uint32_t> member_ptrs;  // access chain -> bank member
  struct Address {
    uint32_t member;
    uint64_t offset;
  };
  std::unordered_map<uint32_t, Address> addresses;     // bank address (integer or pointer)
  uint32_t push_variable = 0;
  uint32_t glsl_set = 0;
  auto value_bound = [&](uint32_t id) {
    const auto it = bound.find(id);
    return it == bound.end() ? kInf : it->second;
  };
  auto escape = [&](uint32_t member) { result.bytes[member] = SpirvConstantRange::kUnbounded; };
  auto note_access = [&](const Address& address) {
    if (result.bytes[address.member] == SpirvConstantRange::kUnbounded) return;
    const uint64_t end = add(address.offset, 16);
    if (end >= SpirvConstantRange::kUnbounded) escape(address.member);
    else if (end > result.bytes[address.member]) result.bytes[address.member] = uint32_t(end);
  };

  for (size_t i = 5; i < count;) {
    const uint32_t word_count = words[i] >> 16;
    const uint16_t opcode = uint16_t(words[i] & 0xFFFF);
    if (!word_count || i + word_count > count) {
      fail_all();
      return result;
    }
    const uint32_t* op = words + i;
    switch (opcode) {
      case kOpExtInstImport:
        // "GLSL.std.450"
        if (word_count >= 3 && op[2] == 0x4C534C47u) glsl_set = op[1];
        break;
      case kOpTypeInt:
        int_types[op[1]] = op[2];
        break;
      case kOpConstant:
        if (int_types.count(op[1])) {
          uint64_t value = op[3];
          if (int_types[op[1]] == 64 && word_count >= 5) value |= uint64_t(op[4]) << 32;
          bound[op[2]] = value;
        }
        break;
      case kOpVariable:
        if (word_count >= 4 && op[3] == kStoragePushConstant) push_variable = op[2];
        break;
      case kOpAccessChain:
      case kOpInBoundsAccessChain:
        if (word_count >= 5 && op[3] == push_variable && push_variable) {
          const uint64_t member = value_bound(op[4]);
          if (member < 3 && word_count == 5) member_ptrs[op[2]] = uint32_t(member);
        }
        break;
      case kOpLoad:
        if (word_count >= 4) {
          if (const auto it = member_ptrs.find(op[3]); it != member_ptrs.end()) {
            addresses[op[2]] = {it->second, 0};
          } else if (const auto at = addresses.find(op[3]); at != addresses.end()) {
            note_access(at->second);
          }
        }
        break;
      case kOpIAdd: {
        const auto a = addresses.find(op[3]);
        const auto b = addresses.find(op[4]);
        if (a != addresses.end() && b != addresses.end()) {
          escape(a->second.member);
          escape(b->second.member);
        } else if (a != addresses.end()) {
          addresses[op[2]] = {a->second.member, add(a->second.offset, value_bound(op[4]))};
        } else if (b != addresses.end()) {
          addresses[op[2]] = {b->second.member, add(b->second.offset, value_bound(op[3]))};
        } else {
          bound[op[2]] = add(value_bound(op[3]), value_bound(op[4]));
        }
        break;
      }
      case kOpIMul:
        bound[op[2]] = mul(value_bound(op[3]), value_bound(op[4]));
        break;
      case kOpShiftLeftLogical: {
        const uint64_t shift = value_bound(op[4]);
        bound[op[2]] = shift < 32 ? mul(value_bound(op[3]), uint64_t(1) << shift) : kInf;
        break;
      }
      case kOpBitwiseAnd:
        bound[op[2]] = std::min(value_bound(op[3]), value_bound(op[4]));
        break;
      case kOpSelect:
        bound[op[2]] = std::max(value_bound(op[4]), value_bound(op[5]));
        if (addresses.count(op[4]) || addresses.count(op[5])) {
          if (addresses.count(op[4])) escape(addresses[op[4]].member);
          if (addresses.count(op[5])) escape(addresses[op[5]].member);
        }
        break;
      case kOpUConvert:
      case kOpSConvert:
      case kOpBitcast:
      case kOpConvertUToPtr:
        if (const auto at = addresses.find(op[3]); at != addresses.end()) {
          addresses[op[2]] = at->second;
        } else {
          bound[op[2]] = value_bound(op[3]);
        }
        break;
      case kOpExtInst:
        if (word_count >= 7 && op[3] == glsl_set) {
          const uint32_t instruction = op[4];
          if (instruction == kGlslUMin || instruction == kGlslSMin) {
            bound[op[2]] = std::min(value_bound(op[5]), value_bound(op[6]));
          } else if ((instruction == kGlslUClamp || instruction == kGlslSClamp) && word_count >= 8) {
            bound[op[2]] = value_bound(op[7]);
          }
        }
        for (uint32_t w = 5; w < word_count; ++w)
          if (const auto at = addresses.find(op[w]); at != addresses.end()) escape(at->second.member);
        break;
      case kOpPhi: {
        uint64_t highest = 0;
        for (uint32_t w = 3; w + 1 < word_count; w += 2) {
          if (addresses.count(op[w])) escape(addresses[op[w]].member);
          highest = std::max(highest, value_bound(op[w]));
        }
        bound[op[2]] = highest;
        break;
      }
      case kOpName:
      case kOpMemberName:
      case kOpDecorate:
      case kOpMemberDecorate:
      case kOpEntryPoint:
        break;
      default:
        // Any other instruction consuming a bank address (store, call, compare,
        // access chain into a pointer, ...) makes that bank unbounded.
        for (uint32_t w = 1; w < word_count; ++w)
          if (const auto at = addresses.find(op[w]); at != addresses.end()) escape(at->second.member);
        break;
    }
    i += word_count;
  }
  return result;
}

}  // namespace rex::graphics::gta4_native
