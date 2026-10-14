#pragma once
#include "resources.h"
#include "../gta4_native/core/geometry.h"
#include <array>
#include <span>
namespace rex::graphics::gta4_metal {
struct VertexCorrectionPlan {
  std::array<uint64_t,gta4_native::kMaximumVertexElementCount> steps{};
  size_t count=0;
  std::span<const uint64_t> values()const{return {steps.data(),count};}
};
inline VertexCorrectionPlan VertexCorrections(const VertexDeclaration& declaration,const ShaderMetadata& shader,
                                              uint32_t stream,uint32_t stride){
  VertexCorrectionPlan plan;
  for(const auto& element:declaration.elements){
    if(element.stream!=stream||element.offset>=stride)continue;
    const auto location=gta4_native::core::ConvertVertexUsageToLocation(element.usage,element.usage_index);
    const auto end=shader.attributes.begin()+shader.attribute_count;
    const auto input=std::find_if(shader.attributes.begin(),end,[&](const auto& a){return a.semantic_location==location;});
    if(input==end)continue;
    const uint32_t n=gta4_native::core::GetVertexElement16BitComponentCount(element.type);
    const uint32_t operation=n?1:element.type==0x1A2187?2:
        element.type==0x182886&&input->scalar_type==MetalVertexScalar::kUnsignedInteger?3:0;
    if(operation)plan.steps[plan.count++]=(uint64_t(element.offset)<<32)|(uint64_t(operation)<<16)|n;
  }
  return plan;
}
}
