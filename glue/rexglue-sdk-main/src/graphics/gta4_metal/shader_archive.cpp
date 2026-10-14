#include "shader_archive.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace rex::graphics::gta4_metal {
namespace {
uint32_t U32(std::span<const std::byte> data, size_t at) {
  uint32_t value = 0;
  for (uint32_t n = 0; n < 4; ++n)
    value |= uint32_t(std::to_integer<uint8_t>(data[at + n])) << (n * 8);
  return value;
}
uint64_t U64(std::span<const std::byte> data, size_t at) {
  return uint64_t(U32(data, at)) | (uint64_t(U32(data, at + 4)) << 32);
}
bool IsUtf8(std::span<const std::byte> data) {
  size_t at = 0;
  for (; at < data.size();) {
    const uint32_t c = std::to_integer<uint8_t>(data[at++]);
    if (!c) return false;
    if (c < 128) continue;
    uint32_t value = 0, count = 0, minimum = 0;
    if (c >= 0xC2 && c <= 0xDF) { value = c & 31; count = 1; minimum = 0x80; }
    else if (c >= 0xE0 && c <= 0xEF) { value = c & 15; count = 2; minimum = 0x800; }
    else if (c >= 0xF0 && c <= 0xF4) { value = c & 7; count = 3; minimum = 0x10000; }
    else return false;
    if (count > data.size() - at) return false;
    for (uint32_t n = 0; n < count; ++n) {
      const uint32_t next = std::to_integer<uint8_t>(data[at++]);
      if ((next & 0xC0) != 0x80) return false;
      value = (value << 6) | (next & 63);
    }
    if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return false;
  }
  return true;
}
}  // namespace

bool MetalShaderArchive::Open(std::span<const std::byte> bytes,
                             const MetalArchiveDigestVerifier& verify_digest,
                             std::string& error) {
  Clear();
  error.clear();
  const auto fail = [&](const char* reason) { error = reason; return false; };
  if (bytes.size() < kMetalArchiveHeaderBytes || bytes.size() > kMetalArchiveMaximumBytes)
    return fail("Metal shader archive has an invalid size");
  if (std::memcmp(bytes.data(), "LRMETAL2", 8) || U32(bytes, 8) != kMetalArchiveVersion ||
      U32(bytes, 12) != kMetalArchiveHeaderBytes || U32(bytes, 16) != kMetalArchiveRecordBytes ||
      U64(bytes, 24) != bytes.size()) return fail("Metal shader archive header is incompatible");
  const uint32_t count = U32(bytes, 20);
  if (!count || count > kMetalArchiveMaximumShaders ||
      count > (bytes.size() - kMetalArchiveHeaderBytes) / kMetalArchiveRecordBytes)
    return fail("Metal shader archive index is invalid");
  const size_t table_end = kMetalArchiveHeaderBytes + size_t(count) * kMetalArchiveRecordBytes;
  if (!verify_digest || !verify_digest(bytes.subspan(kMetalArchiveHeaderBytes),
       std::span<const std::byte, 32>(bytes.data() + 32, 32)))
    return fail("Metal shader archive checksum mismatch");

  std::vector<std::pair<size_t, size_t>> ranges;
  ranges.reserve(size_t(count) * 4);
  std::vector<MetalShaderRecord> records;
  records.reserve(count);
  const auto payload = [&](uint64_t offset, uint64_t length, bool optional,
                           std::span<const std::byte>& value) {
    value = {};
    if (!length) return optional && !offset;
    if (offset < table_end || offset % 16 || offset > bytes.size() || length > bytes.size() - offset)
      return false;
    value = bytes.subspan(size_t(offset), size_t(length));
    ranges.emplace_back(size_t(offset), size_t(offset + length));
    return true;
  };
  uint64_t previous = 0;
  for (uint32_t index = 0; index < count; ++index) {
    const auto row = bytes.subspan(kMetalArchiveHeaderBytes + size_t(index) * kMetalArchiveRecordBytes,
                                   kMetalArchiveRecordBytes);
    MetalShaderRecord record;
    record.hash = U64(row, 0);
    const uint32_t stage = U32(row, 8);
    record.stage = MetalArchiveStage(stage);
    record.used_texture_mask = U32(row, 12);
    record.specialization_constants_mask = U32(row, 16);
    record.output_mask = U32(row, 20);
    record.attribute_count = U32(row, 24);
    record.flags = U32(row, 28);
    if (record.hash <= previous || stage > 1 || (record.used_texture_mask & ~kMetalArchiveTextureMask) ||
        (record.output_mask & ~31u) || (record.flags & ~15u) || U32(row, 76) ||
        record.attribute_count > kMetalArchiveMaximumAttributes ||
        U64(row, 88) != uint64_t(record.attribute_count) * kMetalArchiveAttributeBytes)
      return fail("Metal shader archive contains invalid record metadata");
    previous = record.hash;
    std::span<const std::byte> name, attributes;
    if (!payload(U64(row, 32), U64(row, 40), false, record.early_library) ||
        !payload(U64(row, 48), U64(row, 56), true, record.late_library) ||
        !payload(U64(row, 64), U32(row, 72), false, name) ||
        !payload(U64(row, 80), U64(row, 88), true, attributes))
      return fail("Metal shader archive contains an invalid payload range");
    if (name.size() > 4096 || !IsUtf8(name)) return fail("Metal shader name is invalid UTF-8");
    record.name = {reinterpret_cast<const char*>(name.data()), name.size()};
    if (record.early_library.size() < 4 || std::memcmp(record.early_library.data(), "MTLB", 4) ||
        (!record.late_library.empty() && (record.late_library.size() < 4 ||
                                         std::memcmp(record.late_library.data(), "MTLB", 4))))
      return fail("Metal shader payload is not a library");
    if (bool(record.flags & kMetalShaderLateVariant) != !record.late_library.empty() ||
        ((record.flags & kMetalShaderEarlyTests) && (record.output_mask & 16u)) ||
        ((record.flags & kMetalShaderCoverage) && !(record.output_mask & 1u)) ||
        (stage == 1 && (record.output_mask || record.flags || !record.late_library.empty())) ||
        (stage == 0 && record.attribute_count))
      return fail("Metal shader variant flags are inconsistent");
    uint64_t semantics = 0;
    for (uint32_t a = 0; a < record.attribute_count; ++a) {
      const auto attribute = attributes.subspan(size_t(a) * kMetalArchiveAttributeBytes,
                                                kMetalArchiveAttributeBytes);
      const auto location = U32(attribute, 4), numeric = U32(attribute, 8), components = U32(attribute, 12);
      if (U32(attribute, 0) != a || location >= 64 || numeric > 2 || !components || components > 4 ||
          (semantics & (uint64_t{1} << location))) return fail("Metal vertex attribute is invalid");
      semantics |= uint64_t{1} << location;
      record.attributes[a] = {a, location, MetalVertexScalar(numeric), components};
    }
    records.push_back(record);
  }
  std::sort(ranges.begin(), ranges.end());
  size_t end = table_end;
  for (const auto& [begin, stop] : ranges) {
    if (begin < end || begin - end >= 16 ||
        std::any_of(bytes.begin() + end, bytes.begin() + begin, [](auto b) { return b != std::byte{}; }))
      return fail("Metal shader payloads overlap or have noncanonical padding");
    end = stop;
  }
  if (end != bytes.size()) return fail("Metal shader archive has an unreferenced tail");
  records_ = std::move(records);
  return true;
}

const MetalShaderRecord* MetalShaderArchive::Find(uint64_t hash, MetalArchiveStage stage) const noexcept {
  const auto found = std::lower_bound(records_.begin(), records_.end(), hash,
                                     [](const auto& record, uint64_t value) { return record.hash < value; });
  return found != records_.end() && found->hash == hash && found->stage == stage ? &*found : nullptr;
}
}  // namespace rex::graphics::gta4_metal
