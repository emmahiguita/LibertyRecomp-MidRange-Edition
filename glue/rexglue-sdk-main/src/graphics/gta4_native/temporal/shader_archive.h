#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace rex::graphics::gta4_native::temporal {
enum class ShaderStage : uint32_t { kPixel, kVertex };
// Immutable after Load, allowing spans to remain valid while creating modules.
// Renderer lifetime must enclose every lookup; Vulkan owns its shader modules.
class ShaderArchive {
 public:
  bool Load(const std::filesystem::path&,std::string& error);
  bool Load(std::span<const uint8_t>,std::string& error);
  std::span<const uint32_t> Find(uint64_t hash,ShaderStage,bool overridden,bool late) const;
  size_t size() const { return records_.size(); }
 private:
  struct Record { uint64_t hash; uint32_t flags,offset,count; };
  std::vector<uint32_t> words_;
  std::vector<Record> records_;
};
}  // namespace rex::graphics::gta4_native::temporal
