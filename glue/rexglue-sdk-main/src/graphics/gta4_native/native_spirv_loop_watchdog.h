#pragma once
// Bounds every structured loop of a recompiled shader by a per-invocation
// iteration budget. Xenos loops use 8-bit counters, so a legitimate shader never
// comes close to the budget; a loop that does (stale constants, NaN-driven
// predicates in the control-flow state machine) ends the invocation instead of
// running until the kernel resets the GPU.
//
// Each void function gets one Function-storage counter. Every loop header
// increments it; once the budget is spent the header branches to a new block
// that returns (single-block loops, whose header is the continue target, are
// skipped). Returning (rather than leaving through the merge block) adds no
// edge to the merge, so values that flow out of the loop by dominance stay
// valid. A header ending in OpBranchConditional is split: the header tests the
// budget and a new block repeats the original conditional branch, so OpPhis in
// its targets name the new block as their predecessor.
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace rex::graphics::gta4_native {

struct SpirvLoopWatchdogResult {
  std::vector<uint32_t> words;
  uint32_t guarded_loops = 0;
};

inline std::optional<SpirvLoopWatchdogResult> AddSpirvLoopWatchdog(const std::vector<uint32_t>& in,
                                                                   uint32_t budget) {
  constexpr uint16_t kOpTypeVoid = 19, kOpTypeBool = 20, kOpTypeInt = 21, kOpTypePointer = 32,
                     kOpConstant = 43, kOpFunction = 54, kOpFunctionEnd = 56, kOpVariable = 59,
                     kOpLoad = 61, kOpStore = 62, kOpLabel = 248, kOpBranch = 249,
                     kOpBranchConditional = 250, kOpLoopMerge = 246, kOpReturn = 253,
                     kOpIAdd = 128, kOpULessThan = 176, kOpPhi = 245;
  constexpr uint32_t kStorageFunction = 7;
  if (in.size() < 5 || in[0] != 0x07230203u) return std::nullopt;

  uint32_t bound = in[3];
  uint32_t uint_type = 0, bool_type = 0, uint_function_pointer = 0, void_type = 0;
  size_t first_function = 0;
  struct Split {
    uint32_t header, guard;
  };
  // Blocks targeted by a split header: their OpPhis must name the guard block.
  std::unordered_map<uint32_t, Split> phi_renames;
  std::unordered_map<size_t, uint32_t> guards;  // OpLoopMerge word index -> guard label
  bool void_function = false;
  uint32_t current_label = 0;
  for (size_t i = 5; i < in.size();) {
    const uint32_t count = in[i] >> 16;
    const uint16_t op = uint16_t(in[i] & 0xFFFF);
    if (!count || i + count > in.size()) return std::nullopt;
    const uint32_t* w = in.data() + i;
    if (op == kOpTypeVoid) void_type = w[1];
    if (op == kOpTypeInt && w[2] == 32 && w[3] == 0 && !uint_type) uint_type = w[1];
    if (op == kOpTypeBool && !bool_type) bool_type = w[1];
    if (op == kOpTypePointer && w[2] == kStorageFunction && w[3] == uint_type && uint_type)
      uint_function_pointer = w[1];
    if (op == kOpFunction) {
      if (!first_function) first_function = i;
      void_function = void_type && w[1] == void_type;
    }
    if (op == kOpLabel) current_label = w[1];
    // A header that is its own continue target belongs to the continue
    // construct, where a returning block would break post-dominance.
    if (op == kOpLoopMerge && void_function && w[2] != current_label && i + count < in.size()) {
      const uint32_t* t = in.data() + i + count;
      const uint16_t t_op = uint16_t(t[0] & 0xFFFF);
      if (t_op == kOpBranch) {
        guards[i] = 0;
      } else if (t_op == kOpBranchConditional) {
        const uint32_t guard = bound++;
        guards[i] = guard;
        phi_renames[t[2]] = {current_label, guard};
        phi_renames[t[3]] = {current_label, guard};
      }
    }
    i += count;
  }
  if (guards.empty() || !first_function) return std::nullopt;

  std::vector<uint32_t> globals;
  auto emit_to = [](std::vector<uint32_t>& out, uint16_t op, std::initializer_list<uint32_t> operands) {
    out.push_back(uint32_t(operands.size() + 1) << 16 | op);
    out.insert(out.end(), operands);
  };
  if (!uint_type) {
    uint_type = bound++;
    emit_to(globals, kOpTypeInt, {uint_type, 32, 0});
  }
  if (!bool_type) {
    bool_type = bound++;
    emit_to(globals, kOpTypeBool, {bool_type});
  }
  if (!uint_function_pointer) {
    uint_function_pointer = bound++;
    emit_to(globals, kOpTypePointer, {uint_function_pointer, kStorageFunction, uint_type});
  }
  const uint32_t zero = bound++, one = bound++, limit = bound++;
  emit_to(globals, kOpConstant, {uint_type, zero, 0});
  emit_to(globals, kOpConstant, {uint_type, one, 1});
  emit_to(globals, kOpConstant, {uint_type, limit, budget});

  SpirvLoopWatchdogResult result;
  std::vector<uint32_t>& out = result.words;
  out.reserve(in.size() + in.size() / 6 + globals.size() + 64);
  out.insert(out.end(), in.begin(), in.begin() + 5);

  uint32_t counter = 0;
  bool in_function = false, first_block = false, counter_initialized = false;
  current_label = 0;
  for (size_t i = 5; i < in.size();) {
    const uint32_t count = in[i] >> 16;
    const uint16_t op = uint16_t(in[i] & 0xFFFF);
    const uint32_t* w = in.data() + i;
    if (i == first_function) out.insert(out.end(), globals.begin(), globals.end());

    if (op == kOpFunction) {
      in_function = true;
      first_block = counter_initialized = false;
      counter = 0;
    } else if (op == kOpFunctionEnd) {
      in_function = false;
    }
    // The counter joins the first block's leading OpVariables and is zeroed
    // right after them.
    if (in_function && first_block && !counter_initialized && op != kOpVariable) {
      emit_to(out, kOpStore, {counter, zero});
      counter_initialized = true;
      first_block = false;
    }

    if (op == kOpLoopMerge && counter) {
      if (const auto guard = guards.find(i); guard != guards.end()) {
        const uint32_t* t = in.data() + i + count;
        const uint32_t t_count = t[0] >> 16;
        const uint32_t value = bound++, next = bound++, ok = bound++, expired = bound++;
        emit_to(out, kOpLoad, {uint_type, value, counter});
        emit_to(out, kOpIAdd, {uint_type, next, value, one});
        emit_to(out, kOpStore, {counter, next});
        emit_to(out, kOpULessThan, {bool_type, ok, next, limit});
        out.insert(out.end(), w, w + count);
        if (guard->second) {
          emit_to(out, kOpBranchConditional, {ok, guard->second, expired});
          emit_to(out, kOpLabel, {guard->second});
          out.insert(out.end(), t, t + t_count);
        } else {
          emit_to(out, kOpBranchConditional, {ok, t[1], expired});
        }
        emit_to(out, kOpLabel, {expired});
        out.push_back(uint32_t(1) << 16 | kOpReturn);
        ++result.guarded_loops;
        i += count + t_count;
        continue;
      }
    }

    if (op == kOpLabel) current_label = w[1];
    if (op == kOpPhi) {
      if (const auto rename = phi_renames.find(current_label); rename != phi_renames.end()) {
        const size_t start = out.size();
        out.insert(out.end(), w, w + count);
        for (size_t parent = start + 4; parent < out.size(); parent += 2)
          if (out[parent] == rename->second.header) out[parent] = rename->second.guard;
        i += count;
        continue;
      }
    }
    out.insert(out.end(), w, w + count);

    if (op == kOpLabel && in_function && !counter) {
      counter = bound++;
      emit_to(out, kOpVariable, {uint_function_pointer, counter, kStorageFunction});
      first_block = true;
    }
    i += count;
  }
  if (!result.guarded_loops) return std::nullopt;
  out[3] = bound;
  return result;
}

}  // namespace rex::graphics::gta4_native
