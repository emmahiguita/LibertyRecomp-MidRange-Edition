// Build-time translation only. No SPIRV-Cross dependency in the Metal plugin.
// https://github.com/KhronosGroup/SPIRV-Cross
#include <spirv_msl.hpp>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

std::vector<uint32_t> ApplyMetalColorContract(std::vector<uint32_t> input);

int main(int argc, char** argv) {
  try {
    if (argc != 4) throw std::runtime_error("usage: spirv_to_metal input.spv output.metal metadata.json");
    const auto bytes = std::filesystem::file_size(argv[1]);
    if (bytes < 20 || bytes > 64 * 1024 * 1024 || bytes % sizeof(uint32_t))
      throw std::runtime_error("invalid SPIR-V byte size");
    std::vector<uint32_t> words(bytes / sizeof(uint32_t));
    std::ifstream input(argv[1], std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(words.data()), bytes)) throw std::runtime_error("SPIR-V read failed");
    spirv_cross::CompilerMSL reflection(words);
    const auto entries = reflection.get_entry_points_and_stages();
    if (entries.size() != 1) throw std::runtime_error("expected one entry point");
    const auto stage = entries.front().execution_model;
    if (stage != spv::ExecutionModelVertex && stage != spv::ExecutionModelFragment)
      throw std::runtime_error("unsupported game shader stage");
    const bool pixel = stage == spv::ExecutionModelFragment;
    const auto resources = reflection.get_shader_resources();
    uint32_t outputs = 0;
    reflection.update_active_builtins();
    const bool depth = reflection.has_active_builtin(spv::BuiltInFragDepth, spv::StorageClassOutput);
    const bool coverage = reflection.has_active_builtin(spv::BuiltInSampleMask, spv::StorageClassOutput);
    const bool early = reflection.get_execution_mode_bitset().get(spv::ExecutionModeEarlyFragmentTests);
    if (pixel) {
      for (const auto& resource : resources.stage_outputs) {
        const uint32_t location = reflection.get_decoration(resource.id, spv::DecorationLocation);
        if (location >= 4) throw std::runtime_error("fragment output exceeds title ABI");
        outputs |= 1u << location;
      }
      words = ApplyMetalColorContract(std::move(words));
    }
    spirv_cross::CompilerMSL compiler(std::move(words));
    const auto transformed_entries = compiler.get_entry_points_and_stages();
    compiler.rename_entry_point(transformed_entries.front().name, "shaderMain", stage);
    // The shared override cache is compiled for Vulkan (-fvk-invert-y).
    // Metal's stock title shaders preserve the guest clip-space Y convention.
    // Undo Vulkan's flip at the stage output, including the half-pixel offset,
    // so replacement geometry keeps the same coverage and winding as stock.
    auto common_options = compiler.get_common_options();
    common_options.vertex.flip_vert_y = !pixel;
    compiler.set_common_options(common_options);
    auto options = compiler.get_msl_options();
    options.platform = spirv_cross::CompilerMSL::Options::macOS;
    options.set_msl_version(3, 0);
    options.argument_buffers = true;
    options.argument_buffers_tier = spirv_cross::CompilerMSL::Options::ArgumentBuffersTier::Tier2;
    options.enable_decoration_binding = true;
    options.pad_fragment_output_components = true;
    compiler.set_msl_options(options);
    for (uint32_t set = 0; set < 5; ++set) {
      compiler.set_argument_buffer_device_address_space(set, true);
      spirv_cross::MSLResourceBinding binding{};
      binding.stage = stage;
      binding.desc_set = set;
      binding.binding = 0;
      binding.msl_buffer = set;
      binding.msl_texture = 0;
      binding.msl_sampler = 0;
      compiler.add_msl_resource_binding(binding);
    }
    spirv_cross::MSLResourceBinding push{};
    push.stage = stage;
    push.desc_set = spirv_cross::kPushConstDescSet;
    push.binding = spirv_cross::kPushConstBinding;
    push.msl_buffer = 8;
    compiler.add_msl_resource_binding(push);
    struct Attribute { uint32_t semantic, index, type, components; };
    std::vector<Attribute> attributes;
    if (!pixel) {
      auto inputs = compiler.get_shader_resources().stage_inputs;
      std::sort(inputs.begin(), inputs.end(), [&](const auto& a, const auto& b) {
        return compiler.get_decoration(a.id, spv::DecorationLocation) <
               compiler.get_decoration(b.id, spv::DecorationLocation);
      });
      for (const auto& resource : inputs) {
        const auto& type = compiler.get_type(resource.type_id);
        const uint32_t numeric = type.basetype == spirv_cross::SPIRType::Float ? 0 :
            type.basetype == spirv_cross::SPIRType::UInt ? 2 :
            type.basetype == spirv_cross::SPIRType::Int ? 1 : UINT32_MAX;
        if (numeric == UINT32_MAX || type.columns != 1 || type.width != 32 || !type.array.empty() ||
            type.vecsize < 1 || type.vecsize > 4 || attributes.size() >= 31)
          throw std::runtime_error("unsupported vertex interface");
        const uint32_t location = compiler.get_decoration(resource.id, spv::DecorationLocation);
        const uint32_t index = uint32_t(attributes.size());
        compiler.set_decoration(resource.id, spv::DecorationLocation, index);
        attributes.push_back({location, index, numeric, type.vecsize});
      }
    }
    const std::string source = compiler.compile();
    std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
    output << source;
    if (!output) throw std::runtime_error("MSL write failed");
    std::ofstream metadata(argv[3], std::ios::binary | std::ios::trunc);
    metadata << "{\"stage\":" << (pixel ? 0 : 1) << ",\"outputs\":" << outputs
             << ",\"depth\":" << depth << ",\"coverage\":" << coverage
             << ",\"early\":" << early << ",\"attributes\":[";
    for (size_t i = 0; i < attributes.size(); ++i) {
      if (i) metadata << ',';
      const auto& a = attributes[i];
      metadata << '[' << a.semantic << ',' << a.index << ',' << a.type << ',' << a.components << ']';
    }
    metadata << "]}\n";
    if (!metadata) throw std::runtime_error("metadata write failed");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Metal shader translation: " << error.what() << '\n';
    return 1;
  }
}
