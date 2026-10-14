#include "shader_archive.h"
#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/sha.h>
#endif

#include <array>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace rex::graphics::gta4_metal;

int main(int argc, char** argv) {
  const auto digest = [](std::span<const std::byte> data, std::span<const std::byte, 32> expected) {
    std::array<unsigned char, 32> actual{};
#if defined(__APPLE__)
    CC_SHA256(data.data(), CC_LONG(data.size()), actual.data());
#else
    SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), actual.data());
#endif
    return std::memcmp(actual.data(), expected.data(), actual.size()) == 0;
  };
  for (int argument = 1; argument < argc; ++argument) {
    std::ifstream input(argv[argument], std::ios::binary);
    if (!input) return 2;
    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    MetalShaderArchive archive;
    std::string error;
    const bool valid = archive.Open({reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()}, digest, error);
    if (valid) {
      assert(error.empty());
      for (const auto& record : archive.records()) {
        assert(archive.Find(record.hash, record.stage) == &record);
        const auto wrong = record.stage == MetalArchiveStage::kPixel ? MetalArchiveStage::kVertex : MetalArchiveStage::kPixel;
        assert(archive.Find(record.hash, wrong) == nullptr);
      }
      const auto records = archive.records().size();
      assert(!archive.Open({}, digest, error));
      assert(!error.empty() && archive.records().empty());
      std::cout << argument << "\t1\t" << records << '\n';
    } else {
      assert(!error.empty() && archive.records().empty());
      std::cout << argument << "\t0\t0\n";
    }
  }
}
