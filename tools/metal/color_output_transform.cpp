#include <stdexcept>
#include <string>
#include <vector>
#include "native_color_output_spirv.h"

std::vector<uint32_t> ApplyMetalColorContract(std::vector<uint32_t> input) {
  std::string error;
  auto output = rex::graphics::gta4_native::AddNativeColorOutputEpilogue(input, &error);
  if (!output) throw std::runtime_error("native color output: " + error);
  return std::move(*output);
}
