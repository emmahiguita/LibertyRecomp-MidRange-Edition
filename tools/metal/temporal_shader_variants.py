#!/usr/bin/env python3
"""Offline motion-output variants of the existing, compiled title shader source.

The original vertex/pixel calculations remain intact. A second vertex evaluation
uses the previous immutable constant bank (including bone matrices), not a second
render or an estimate based on camera movement. Metadata remains the original
title interface: the temporal pipeline owns the two additional host attachments.
"""
from __future__ import annotations
import argparse
import concurrent.futures
import dataclasses
from pathlib import Path
import re
import sys
import shader_archive as archive

PARAMETERS = """constant Texture2DDescriptorHeap* g_Texture2DDescriptorHeap [[buffer(0)]],
constant Texture2DArrayDescriptorHeap* g_Texture2DArrayDescriptorHeap [[buffer(1)]],
constant Texture3DDescriptorHeap* g_Texture3DDescriptorHeap [[buffer(2)]],
constant TextureCubeDescriptorHeap* g_TextureCubeDescriptorHeap [[buffer(3)]],
constant SamplerDescriptorHeap* g_SamplerDescriptorHeap [[buffer(4)]],
constant LibertyTemporalDraw& temporal [[buffer(6)]],
constant PushConstants& previousConstants [[buffer(7)]],
constant PushConstants& g_PushConstants [[buffer(8)]]"""
HEAPS = "g_Texture2DDescriptorHeap,g_Texture2DArrayDescriptorHeap,g_Texture3DDescriptorHeap,g_TextureCubeDescriptorHeap,g_SamplerDescriptorHeap"
CONTRACT = """
#ifdef __air__
struct LibertyTemporalDraw {
    float2 inputExtent;
    float2 currentJitterClip;
    float2 previousJitterClip;
    uint validHistory;
    uint reactive;
    uint uiMode;
    uint padding0;
    uint padding1;
    uint padding2;
};
#endif
"""


def ui_outputs(color: str, destination_alpha: str = "1.0f") -> str:
    return f"""
    float4 uiColor={color};
    result.libertyUiAdd=float4(0);result.libertyUiTransmit=float4(1);
    if(temporal.uiMode==1u) {{result.libertyUiAdd=float4(uiColor.rgb,1);result.libertyUiTransmit=float4(0);}}
    if(temporal.uiMode==2u) {{result.libertyUiAdd=float4(uiColor.rgb*uiColor.a,uiColor.a);result.libertyUiTransmit=float4(1-uiColor.a);}}
    if(temporal.uiMode==3u) {{result.libertyUiAdd=uiColor;result.libertyUiTransmit=float4(1-uiColor.a);}}
    if(temporal.uiMode==4u) {{result.libertyUiAdd=float4(uiColor.rgb,0);}}
    if(temporal.uiMode==5u) {{result.libertyUiAdd=float4(uiColor.rgb*uiColor.a,0);}}
    if(temporal.uiMode==6u) {{result.libertyUiAdd=uiColor;result.libertyUiTransmit=uiColor;}}
    if(temporal.uiMode==7u) {{float coverage=clamp({destination_alpha},0.0f,1.0f);result.libertyUiAdd=float4(uiColor.rgb*coverage,coverage);result.libertyUiTransmit=float4(1-coverage);}}
    if(temporal.uiMode==8u) {{float coverage=clamp({destination_alpha},0.0f,1.0f);result.libertyUiAdd=float4(uiColor.rgb*coverage,coverage);}}
"""


def transform(source: str, stage: int) -> str:
    if stage not in (archive.VERTEX, archive.PIXEL):
        raise ValueError("invalid temporal shader stage")
    if "LibertyTemporalDraw" in source:
        raise ValueError("source is already a temporal variant")
    result_type = "Interpolators" if stage == archive.VERTEX else "PixelShaderOutput"
    marker = result_type + " shaderMain("
    if source.count(marker) != 1:
        return transform_override(source, stage)
    body_start = source.index("{", source.index(marker))
    prefix, body = source[:body_start], source[body_start:]
    # Remove resource attributes only from the now non-entry original function.
    start = prefix.index(marker)
    signature = re.sub(r"\[\[(?:buffer\(\d+\)|stage_in|front_facing)\]\]", "", prefix[start:])
    prefix = prefix[:start] + signature.replace(marker, result_type + " liberty_temporal_original(", 1)
    prefix = prefix.replace("[[vertex]]", "").replace("[[fragment]]", "").replace("[[early_fragment_tests]]", "")
    interpolators = "struct Interpolators\n{\n#ifdef __air__"
    if prefix.count(interpolators) != 1:
        raise ValueError("stock interpolator contract is unavailable")
    prefix = prefix.replace(interpolators, interpolators + """
    float4 libertyCurrentClip [[user(LIBERTY_CURRENT_CLIP)]];
    float4 libertyPreviousClip [[user(LIBERTY_PREVIOUS_CLIP)]];""", 1)
    if stage == archive.VERTEX:
        wrapper = f"""
#ifdef __air__
vertex Interpolators shaderMain({PARAMETERS}, VertexShaderInput input [[stage_in]]) {{
    Interpolators current=liberty_temporal_original({HEAPS},g_PushConstants,input);
    float2 currentHalf=*(reinterpret_cast<device float2*>(g_PushConstants.SharedConstants+552));
    current.libertyCurrentClip=current.oPos;
    current.libertyCurrentClip.xy-=currentHalf*current.oPos.w;
    current.oPos.xy+=temporal.currentJitterClip*current.oPos.w;
    current.libertyPreviousClip=current.libertyCurrentClip;
    if(temporal.validHistory) {{
        Interpolators previous=liberty_temporal_original({HEAPS},previousConstants,input);
        float2 previousHalf=*(reinterpret_cast<device float2*>(previousConstants.SharedConstants+552));
        current.libertyPreviousClip=previous.oPos;
        current.libertyPreviousClip.xy-=previousHalf*previous.oPos.w;
        float2 currentDepthRange=*(reinterpret_cast<device float2*>(g_PushConstants.SharedConstants+1264));
        float2 previousDepthRange=*(reinterpret_cast<device float2*>(previousConstants.SharedConstants+1264));
        // Native history depth uses the standard projection's device-depth
        // convention. Do not invent that convention for a remapped viewport.
        if(!all(currentDepthRange==float2(0,1)) || !all(previousDepthRange==float2(0,1)))
            current.libertyPreviousClip.z=-current.libertyPreviousClip.w;
    }}
    return current;
}}
#endif
"""
    else:
        output_marker = "struct PixelShaderOutput\n{\n#ifdef __air__"
        if prefix.count(output_marker) != 1:
            raise ValueError("stock fragment-output contract is unavailable")
        prefix = prefix.replace(output_marker, output_marker + """
    float2 libertyMotion [[color(4)]];
    float libertyReactive [[color(5)]];
    float4 libertyUiAdd [[color(6)]];
    float4 libertyUiTransmit [[color(7)]];""", 1)
        early = "[[early_fragment_tests]]" if "[[early_fragment_tests]]" in source else ""
        wrapper = f"""
#ifdef __air__
{early}
fragment PixelShaderOutput shaderMain(Interpolators input [[stage_in]],bool iFace [[front_facing]],{PARAMETERS}) {{
    PixelShaderOutput result=liberty_temporal_original(input,iFace,{HEAPS},g_PushConstants);
    bool valid=temporal.validHistory && all(isfinite(input.libertyCurrentClip)) && all(isfinite(input.libertyPreviousClip)) &&
        input.libertyCurrentClip.w>1.0e-6f && input.libertyPreviousClip.w>1.0e-6f;
    float2 velocity=float2(0);
    if(valid) velocity=(input.libertyPreviousClip.xy/input.libertyPreviousClip.w-input.libertyCurrentClip.xy/input.libertyCurrentClip.w)*temporal.inputExtent*float2(0.5f,-0.5f);
    valid=valid && all(isfinite(velocity)) && all(abs(velocity)<=65504.0f);
    result.libertyMotion=valid?velocity:float2(0);
    result.libertyReactive=(!valid || temporal.reactive)?1.0f:0.0f;
    {ui_outputs("result.oC0" if re.search(r"float4 oC0", prefix) else "float4(0)")}
    if(temporal.uiMode==0u) {{
        float previousDepth=valid?input.libertyPreviousClip.z/input.libertyPreviousClip.w:-1.0f;
        result.libertyUiAdd=float4(isfinite(previousDepth) && previousDepth>=0 && previousDepth<=1 ? previousDepth : -1.0f,0,0,0);
    }}
    return result;
}}
#endif
"""
    if stage == archive.PIXEL and re.search(r"float4 oC0", prefix):
        ui_wrapper=wrapper.replace("shaderMain(","shaderMainUI(",1).replace(
            PARAMETERS+")",PARAMETERS+",float4 libertyDestination [[color(0)]])",1)
        ui_wrapper=ui_wrapper.replace(ui_outputs("result.oC0"),ui_outputs("result.oC0","libertyDestination.a"))
        wrapper+=ui_wrapper
    return CONTRACT + prefix + body + wrapper


def transform_override(source: str, stage: int) -> str:
    entry = re.search(r"(?P<early>\[\[\s*early_fragment_tests\s*\]\]\s*)?(?P<stage>vertex|fragment)\s+(?P<output>\w+)\s+shaderMain\((?P<parameters>[^\n]+)\)\s*\{", source)
    if not entry or (entry["stage"] == "vertex") != (stage == archive.VERTEX):
        raise ValueError("converted override entry-point contract is unavailable")
    parameters = entry["parameters"]
    stripped = re.sub(r"\[\[.*?\]\]", "", parameters)
    arguments = []
    for parameter in stripped.split(","):
        variable = re.search(r"\b(\w+)\s*$", parameter)
        if not variable:
            raise ValueError("unsupported override parameter")
        arguments.append(variable[1])
    input_match = re.search(r"(\w+)\s+(\w+)\s*\[\[stage_in\]\]", parameters)
    if not input_match:
        raise ValueError("override has no vertex-stage interface")
    input_type, input_name = input_match.groups()
    output_type = entry["output"]
    replacement = f"{output_type} liberty_temporal_override_original({stripped}) {{"
    result = source[:entry.start()] + replacement + source[entry.end():]
    clip_members = """
    float4 libertyCurrentClip [[user(LIBERTY_CURRENT_CLIP)]];
    float4 libertyPreviousClip [[user(LIBERTY_PREVIOUS_CLIP)]];"""
    target_struct = output_type if stage == archive.VERTEX else input_type
    opening = re.search(r"struct " + re.escape(target_struct) + r"\s*\{", result)
    if not opening:
        raise ValueError("override stage-interface structure unavailable")
    result = result[:opening.end()] + clip_members + result[opening.end():]
    if stage == archive.VERTEX:
        output = re.search(r"struct " + re.escape(output_type) + r"\s*\{(.*?)\};", source, re.S)
        position = re.search(r"float4 (\w+)\s*\[\[position\]\]", output[1]) if output else None
        if not position or "g_PushConstants" not in arguments:
            raise ValueError("override clip-space/constant contract unavailable")
        previous_args = ["previousConstants" if a == "g_PushConstants" else a for a in arguments]
        extra = ", constant LibertyTemporalDraw& temporal [[buffer(6)]], constant type_ConstantBuffer_PushConstants& previousConstants [[buffer(7)]]"
        body = f"""
    auto current=liberty_temporal_override_original({','.join(arguments)});
    current.libertyCurrentClip=current.{position[1]};
    current.libertyCurrentClip.xy-=(*(reinterpret_cast<device const float2*>(g_PushConstants.SharedConstants+552)))*current.{position[1]}.w;
    current.{position[1]}.xy+=temporal.currentJitterClip*current.{position[1]}.w;
    current.libertyPreviousClip=current.libertyCurrentClip;
    if(temporal.validHistory) {{
        auto previous=liberty_temporal_override_original({','.join(previous_args)});
        current.libertyPreviousClip=previous.{position[1]};
        current.libertyPreviousClip.xy-=(*(reinterpret_cast<device const float2*>(previousConstants.SharedConstants+552)))*previous.{position[1]}.w;
        float2 currentDepthRange=*(reinterpret_cast<device const float2*>(g_PushConstants.SharedConstants+1264));
        float2 previousDepthRange=*(reinterpret_cast<device const float2*>(previousConstants.SharedConstants+1264));
        if(!all(currentDepthRange==float2(0,1)) || !all(previousDepthRange==float2(0,1)))
            current.libertyPreviousClip.z=-current.libertyPreviousClip.w;
    }}
    return current;
"""
    else:
        output = re.search(r"struct " + re.escape(output_type) + r"\s*\{", result)
        if not output:
            raise ValueError("override fragment-output structure unavailable")
        members = """
    float2 libertyMotion [[color(4)]];
    float libertyReactive [[color(5)]];
    float4 libertyUiAdd [[color(6)]];
    float4 libertyUiTransmit [[color(7)]];"""
        result = result[:output.end()] + members + result[output.end():]
        color = re.search(r"float4 (\w+)\s*\[\[color\(0\)\]\]", source)
        extra = ", constant LibertyTemporalDraw& temporal [[buffer(6)]]"
        body = f"""
    auto result=liberty_temporal_override_original({','.join(arguments)});
    bool valid=temporal.validHistory && all(isfinite({input_name}.libertyCurrentClip)) && all(isfinite({input_name}.libertyPreviousClip)) &&
        {input_name}.libertyCurrentClip.w>1.0e-6f && {input_name}.libertyPreviousClip.w>1.0e-6f;
    float2 velocity=float2(0);
    if(valid) velocity=({input_name}.libertyPreviousClip.xy/{input_name}.libertyPreviousClip.w-{input_name}.libertyCurrentClip.xy/{input_name}.libertyCurrentClip.w)*temporal.inputExtent*float2(0.5f,-0.5f);
    valid=valid && all(isfinite(velocity)) && all(abs(velocity)<=65504.0f);
    result.libertyMotion=valid?velocity:float2(0);result.libertyReactive=(!valid || temporal.reactive)?1.0f:0.0f;
    {ui_outputs('result.' + color[1] if color else 'float4(0)')}
    if(temporal.uiMode==0u) {{
        float previousDepth=valid?{input_name}.libertyPreviousClip.z/{input_name}.libertyPreviousClip.w:-1.0f;
        result.libertyUiAdd=float4(isfinite(previousDepth) && previousDepth>=0 && previousDepth<=1 ? previousDepth : -1.0f,0,0,0);
    }}
    return result;
"""
    wrapper=f"\n{entry['early'] or ''}{entry['stage']} {output_type} shaderMain({parameters}{extra}) {{\n{body}\n}}\n"
    if stage == archive.PIXEL and color:
        ui_wrapper=wrapper.replace("shaderMain(","shaderMainUI(",1).replace(
            parameters+extra+")",parameters+extra+",float4 libertyDestination [[color(0)]])",1)
        ui_wrapper=ui_wrapper.replace(ui_outputs('result.'+color[1]),ui_outputs('result.'+color[1],'libertyDestination.a'))
        wrapper+=ui_wrapper
    return CONTRACT+result+wrapper


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--sources", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--hash", action="append", default=[])
    parser.add_argument("--deployment", default="26.0")
    parser.add_argument("--audit-only", action="store_true")
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    records = archive.parse_archive(args.archive.read_bytes())
    chosen = {int(value, 16) for value in args.hash}
    found: set[int] = set()
    compiled = []
    compiler = None if args.audit_only else archive.MetalCompiler(args.work, "macosx", args.deployment)
    selected = [r for r in records if not chosen or r.shader_hash in chosen]
    def prepare(record):
        variants = []
        for suffix in ("early", "late") if record.late else ("early",):
            path = args.sources / f"{record.shader_hash:016X}.{suffix}.metal"
            source = transform(path.read_text(), record.stage)
            if compiler:
                args.work.mkdir(parents=True, exist_ok=True)
                (args.work / path.name).write_text(source)
                variants.append(compiler.compile(source))
        if compiler:
            return dataclasses.replace(record, early=variants[0], late=variants[1] if record.late else b"")
        return record
    if not 1 <= args.jobs <= 4:
        raise ValueError("temporal compiler jobs must be in [1,4]")
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for record in pool.map(prepare, selected):
            found.add(record.shader_hash)
            compiled.append(record)
    if chosen - found:
        raise ValueError("requested temporal shader is absent from the stock inventory")
    if compiler:
        archive._atomic_write(args.output, archive.build_archive(compiled))
    print(f"temporal_shader_variants {'audited' if args.audit_only else 'compiled'}={len(found)}")

if __name__ == "__main__":
    main()
