// Transform every archived temporal pixel shader with the exact runtime color
// epilogue; the emitted files are independently checked with spirv-val.
#include "../../src/graphics/gta4_native/native_color_output_spirv.h"
#include "../../src/graphics/gta4_native/temporal/shader_archive.h"

#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

using namespace rex::graphics::gta4_native;
int main(int argc, char** argv) {
  if (argc != 3) return 1;
  temporal::ShaderArchive archive;
  std::string error;
  if (!archive.Load(argv[1], error)) { std::cerr << error; return 1; }
  std::ifstream input(argv[1], std::ios::binary);
  std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
  std::vector<uint32_t> words(bytes.size() / sizeof(uint32_t));
  std::memcpy(words.data(), bytes.data(), bytes.size());
  std::filesystem::create_directories(argv[2]);
  size_t count = 0;
  for (uint32_t i = 0; i < words[3]; ++i) {
    const auto* record = words.data() + 4 + size_t(i) * 5;
    const auto flags = record[2];
    if (flags & 1u) continue;
    const auto hash = uint64_t(record[0]) | (uint64_t(record[1]) << 32);
    const auto original = archive.Find(hash, temporal::ShaderStage::kPixel, flags & 2u, flags & 4u);
    const auto transformed = AddNativeColorOutputEpilogue(original, &error, 0x70u);
    if (!transformed) { std::cerr << "record " << i << ": " << error; return 1; }
    // The unqualified stock path must refuse the extra MRT contract; only the
    // explicit passthrough mask may admit host motion/reactive/depth outputs.
    assert(!AddNativeColorOutputEpilogue(original));
    std::ofstream output(std::filesystem::path(argv[2]) / (std::to_string(i) + ".spv"), std::ios::binary);
    output.write(reinterpret_cast<const char*>(transformed->data()), transformed->size() * sizeof(uint32_t));
    if (!output) return 1;
    ++count;
  }
  std::cout << "validated temporal pixel epilogues: " << count << '\n';
  return count ? 0 : 1;
}
