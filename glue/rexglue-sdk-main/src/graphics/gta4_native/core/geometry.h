#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <rex/graphics/gta4_native/title_commands.h>
namespace rex::graphics::gta4_native::core {
inline uint32_t ConvertVertexUsageToLocation(uint8_t usage, uint8_t usage_index) {
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

inline void CopyGuestWordsToHost(uint8_t* destination, const uint8_t* source, size_t size) {
  size_t offset = 0;
  while (size - offset >= sizeof(uint32_t)) {
    uint32_t word;
    std::memcpy(&word, source + offset, sizeof(word));
    word = __builtin_bswap32(word);
    std::memcpy(destination + offset, &word, sizeof(word));
    offset += sizeof(word);
  }
  if (offset != size) {
    std::memcpy(destination + offset, source + offset, size - offset);
  }
}

inline uint32_t GetVertexElement16BitComponentCount(uint32_t type) {
  switch (type) {
    case 0x2C2359:
    case 0x2C2159:
    case 0x2C2059:
    case 0x2C235F:
      return 2;
    case 0x1A235A:
    case 0x1A215A:
    case 0x1A205A:
    case 0x1A2360:
      return 4;
    default:
      return 0;
  }
}

inline uint32_t GetFloat32VertexElementComponentCount(uint32_t type) {
  switch (type) {
    case 0x2C83A4:
      return 1;
    case 0x2C23A5:
      return 2;
    case 0x2A23B9:
      return 3;
    case 0x1A23A6:
      return 4;
    default:
      return 0;
  }
}

// Float and ordinary 32-bit fields only need the common endian conversion.
// Detect actual correction work, independent of declaration or shader identity.
template <typename Declaration, typename Shader>
bool HasVertexPayloadCorrections(const Declaration& declaration, const Shader& shader,
                                 uint32_t stream) {
  for (const auto& element : declaration.elements) {
    if (element.stream != stream) continue;
    const auto input = std::find_if(shader.vertex_inputs.begin(), shader.vertex_inputs.end(),
        [&](const auto& value) { return value.location == ConvertVertexUsageToLocation(element.usage, element.usage_index); });
    if (input == shader.vertex_inputs.end()) continue;
    using NumericType = std::remove_cvref_t<decltype(input->numeric_type)>;
    if (GetVertexElement16BitComponentCount(element.type) || element.type == 0x1A2187 ||
        (element.type == 0x182886 && input->numeric_type == NumericType::kUnsignedInteger)) return true;
  }
  return false;
}

struct VertexPayloadConversionCounts {
  uint64_t components_16 = 0;
  uint64_t dec3n = 0;
  uint64_t color_uint = 0;
};

template <typename Declaration, typename Shader>
VertexPayloadConversionCounts ConvertGuestVertexPayload(uint8_t* destination, const uint8_t* source,
                                                        size_t size, const Declaration& declaration,
                                                        const Shader& shader,
                                                        uint32_t vertex_stream,
                                                        uint32_t stream_offset, uint32_t stride) {
  VertexPayloadConversionCounts counts;
  CopyGuestWordsToHost(destination, source, size);
  if (!stride || stream_offset >= size) {
    return counts;
  }

  for (const VertexElement& element : declaration.elements) {
    if (element.stream != vertex_stream || element.offset >= stride) {
      continue;
    }
    const auto shader_input = std::find_if(
        shader.vertex_inputs.begin(), shader.vertex_inputs.end(), [&element](const auto& input) {
          return input.location == ConvertVertexUsageToLocation(element.usage, element.usage_index);
        });
    if (shader_input == shader.vertex_inputs.end()) {
      continue;
    }

    const uint32_t component_16_count = GetVertexElement16BitComponentCount(element.type);
    using NumericType = std::remove_cvref_t<decltype(shader_input->numeric_type)>;
    const bool unsigned_color = element.type == 0x182886 &&
        shader_input->numeric_type == NumericType::kUnsignedInteger;
    if (!component_16_count && element.type != 0x1A2187 && !unsigned_color) continue;
    const size_t element_size =
        component_16_count ? size_t(component_16_count) * sizeof(uint16_t) : sizeof(uint32_t);
    for (size_t vertex_offset = size_t(stream_offset) + element.offset;
         vertex_offset <= size - std::min(size, element_size); vertex_offset += stride) {
      if (element_size > size - vertex_offset) {
        break;
      }
      if (component_16_count) {
        for (uint32_t component = 0; component < component_16_count; ++component) {
          uint16_t value;
          const size_t component_offset = vertex_offset + size_t(component) * sizeof(uint16_t);
          std::memcpy(&value, source + component_offset, sizeof(value));
          value = __builtin_bswap16(value);
          std::memcpy(destination + component_offset, &value, sizeof(value));
        }
        ++counts.components_16;
      } else if (element.type == 0x1A2187) {
        uint32_t value;
        std::memcpy(&value, destination + vertex_offset, sizeof(value));
        value = (value & 0x3FFFFFFF) | 0x40000000;
        std::memcpy(destination + vertex_offset, &value, sizeof(value));
        ++counts.dec3n;
      } else if (element.type == 0x182886) {
        using NumericType = std::remove_cvref_t<decltype(shader_input->numeric_type)>;
        if (shader_input->numeric_type != NumericType::kUnsignedInteger) {
          continue;
        }
        std::swap(destination[vertex_offset], destination[vertex_offset + 2]);
        ++counts.color_uint;
      }
    }
  }
  return counts;
}


}
