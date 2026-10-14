#!/usr/bin/env python3
"""Compile existing native-renderer HLSL overrides to Metal, preserving their policy."""
from __future__ import annotations

import argparse
from dataclasses import asdict
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

from shader_archive import (Attribute, Shader, MetalCompiler, build_archive, parse_archive,
                            _atomic_write, coverage_source, OUTPUT_HELPERS, PIXEL, VERTEX,
                            FLAG_NATIVE_OUTPUT, FLAG_COVERAGE, FLAG_EARLY_TESTS,
                            FLAG_LATE_VARIANT, SCALAR_CODES)


def load_module(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load {path}")
    result = importlib.util.module_from_spec(spec)
    sys.modules[name] = result
    spec.loader.exec_module(result)
    return result


def run(args: list[str]) -> str:
    result = subprocess.run(args, capture_output=True, text=True, timeout=180)
    if result.returncode:
        raise RuntimeError(f"Tool failed: {' '.join(args)}\n{result.stdout}{result.stderr}")
    return result.stdout


def function_span(text: str) -> tuple[int, int]:
    match = re.search(r"\b(?:vertex|fragment)\s+\w+\s+shaderMain\s*\(", text)
    if not match:
        raise ValueError("Cannot identify generated Metal entry point")
    depth = 1
    for index in range(match.end(), len(text)):
        if text[index] == '(':
            depth += 1
        elif text[index] == ')':
            depth -= 1
            if depth == 0:
                return match.start(), index
    raise ValueError("Unterminated entry-point signature")


def adapt_msl(text: str, stage: int, coverage: str) -> tuple[str, tuple[Attribute, ...], int, int]:
    # Every runtime override uses the existing title binding ABI: resource sets
    # at buffers 0..4 and three constant-bank addresses at buffer 8.
    text, pushes = re.subn(r"(\bg_PushConstants\s*)\[\[buffer\(\d+\)\]\]",
                          r"\1[[buffer(8)]]", text)
    if pushes != 1:
        raise ValueError("Override does not expose the title push-constant ABI")
    for match in re.finditer(r"spvDescriptorSet(\d+)\s*\[\[buffer\((\d+)\)\]\]", text):
        if match.group(1) != match.group(2) or int(match.group(1)) > 4:
            raise ValueError("SPIRV-Cross resource binding differs from the title ABI")
    # DXC names preserve HLSL semantic identity; Metal user attributes must agree
    # with stock MSL, not the SPIR-V compiler's independently assigned locations.
    text = re.sub(r"(\b(?:in|out)_var_([A-Za-z]+\d+)\s*)\[\[user\(locn\d+\)\]\]",
                  lambda m: m.group(1) + '[[user(' + m.group(2) + ')]]', text)
    if '[[user(locn' in text:
        raise ValueError("Unmapped generated inter-stage semantic")
    attrs: list[Attribute] = []
    if stage == VERTEX:
        pattern = r"\b(float|uint|int)([1-4]?)\s+in_var_([A-Z]+)(\d*)\s*\[\[attribute\((\d+)\)\]\]"
        declarations = list(re.finditer(pattern, text))
        if len(declarations) != text.count('[[attribute('):
            raise ValueError("Unrecognized override vertex attributes")
        semantic = {('POSITION',0):0, ('POSITIONT',0):0, ('NORMAL',0):4,
                    ('TANGENT',0):8, ('BINORMAL',0):12, ('COLOR',0):17,
                    ('BLENDINDICES',0):18, ('BLENDWEIGHT',0):19}
        semantic.update({('TEXCOORD',i):13+i if i<4 else 16+i for i in range(16)})
        for index, match in enumerate(sorted(declarations, key=lambda m: int(m.group(5)))):
            key = (match.group(3), int(match.group(4) or '0'))
            if key not in semantic:
                raise ValueError(f"Unsupported override semantic {key}")
            attrs.append(Attribute(index, semantic[key], SCALAR_CODES[match.group(1)], int(match.group(2) or '1')))
        remap = {int(m.group(5)):i for i,m in enumerate(sorted(declarations,key=lambda m:int(m.group(5))))}
        text = re.sub(r"\[\[attribute\((\d+)\)\]\]", lambda m:f"[[attribute({remap[int(m.group(1))]})]]",text)
        return text, tuple(attrs), 0, 0
    outputs = {int(m.group(2)):m.group(1) for m in re.finditer(r"\b(\w+)\s*\[\[color\((\d+)\)\]\]",text)}
    if any(i>3 for i in outputs):
        raise ValueError("Override has unsupported color output")
    output_mask = sum(1<<i for i in outputs)
    if re.search(r"\[\[depth\(",text):
        output_mask |= 16
    flags = FLAG_NATIVE_OUTPUT
    start, end = function_span(text)
    signature = text[start:end]
    position = re.search(r"\b(\w+)\s*\[\[position\]\]",signature)
    position_name = position.group(1) if position else 'libertyPosition'
    if not position:
        text = text[:end] + ', float4 libertyPosition [[position]]' + text[end:]
    mask = re.search(r"\b(\w+)\s*\[\[sample_mask\]\]",text)
    mask_name = mask.group(1) if mask else 'libertySampleMask'
    if 0 in outputs and not mask:
        match = re.search(r"struct shaderMain_out\s*\{",text)
        if not match:
            raise ValueError("Missing generated fragment output structure")
        text=text[:match.end()]+'\n    uint libertySampleMask [[sample_mask]];'+text[match.end():]
    helper = OUTPUT_HELPERS.replace('#if defined(__air__)','').replace('#endif','')
    if 0 in outputs:
        helper += '\n' + coverage_source(coverage)
        flags |= FLAG_COVERAGE
    insertion = text.index('struct shaderMain_out')
    text = text[:insertion]+helper+'\n'+text[insertion:]
    lines=['{']
    if 0 in outputs:
        operation = '&=' if mask else '='
        lines += ['    const uint2 libertyPixel = uint2('+position_name+'.xy);',
                  '    const device uint* libertyWords = reinterpret_cast<const device uint*>(g_PushConstants.SharedConstants);',
                  '    out.'+mask_name+' '+operation+' liberty_metal_contract::ComputeNativeAlphaToMask(out.'+outputs[0]+'.w, libertyPixel.x, libertyPixel.y, libertyWords[184], libertyWords[185]);']
    for target, name in outputs.items():
        offset=0x360+48*target
        lines.append(f'    out.{name} = libertyColorOutput(out.{name}, *(reinterpret_cast<device const LibertyColorOutput*>(g_PushConstants.SharedConstants + {offset}ul)));')
    lines += ['    return out;','}']
    text,count=re.subn(r'\breturn\s+out\s*;', '\n'.join(lines),text)
    if not count:
        raise ValueError('Generated override has no output return')
    if re.search(r'\[\[\s*early_fragment_tests\s*\]\]',text):
        flags |= FLAG_EARLY_TESTS
    return text, (), output_mask, flags


def compile_all(repo: Path, out: Path, work: Path) -> dict:
    root=repo/'LibertyRecompLib/shader_overrides'
    original=(root/'manifest.json').read_bytes()
    compiler=load_module(repo/'glue/rexglue-sdk-main/tools/gta4_shader_override_compiler.py','liberty_existing_override_compiler')
    entries=compiler.load_manifest(root/'manifest.json',root)
    stock_reader=load_module(repo/'tools/validate_shader_preservation.py','liberty_override_stock_reader')
    cache=repo/'LibertyRecompLib/shader/shader_cache.cpp'
    stock={e.shader_hash:e for e in stock_reader.RANGES.parse_entries(cache.read_text(),cache)}
    coverage=(repo/'glue/rexglue-sdk-main/src/graphics/gta4_native/alpha_to_coverage_util.h').read_text()
    work.mkdir(parents=True,exist_ok=True)
    metal=MetalCompiler(work/'compiled','macosx','26.0')
    cross=shutil.which('spirv-cross');dxc=shutil.which('dxc');glslang=shutil.which('glslangValidator')
    if not cross or not dxc:
        raise RuntimeError('Existing DXC and SPIRV-Cross command-line tools are required')
    shaders=[];records=[]
    for entry in entries:
        key=f"{entry['hash']:016X}"
        primary=None;late=b'';attrs=();outputs=0;flags=0
        variants=(False,True) if compiler.requires_late_fragment_tests(entry) else (False,)
        for is_late in variants:
            label=key+('.late' if is_late else '.early')
            spirv=work/(label+'.spv');msl=work/(label+'.metal')
            compiler.compile_shader(entry,spirv,root,dxc,None,glslang,
                ('XENOS_RECOMP_LATE_FRAGMENT_TESTS',) if is_late else ())
            compiler.validate_spirv(spirv.read_bytes(),compiler.SPIRV_EXECUTION_MODELS[entry['stage_name']],entry['entry_point'],forbid_early_fragment_tests=is_late)
            command=[cross,str(spirv),'--msl','--msl-version','30000','--msl-argument-buffers','--msl-argument-buffer-tier','1']
            for group in range(5):command += ['--msl-device-argument-buffer',str(group)]
            if entry['stage_name']=='vertex':command += ['--flip-vert-y']
            command += ['--output',str(msl)]
            run(command)
            prepared,attrs,outputs,variant_flags=adapt_msl(msl.read_text(),entry['stage'],coverage)
            _atomic_write(msl,prepared.encode())
            library=metal.compile(prepared)
            if is_late:
                late=library;flags|=FLAG_LATE_VARIANT
            else:
                primary=library;flags=variant_flags
        if primary is None:raise ValueError('Missing primary override variant')
        texture_mask=entry['used_texture_mask']
        if texture_mask==0xFFFFFFFF:
            if entry['hash'] not in stock:raise ValueError(f'{key}: no stock texture-use metadata')
            texture_mask=stock[entry['hash']].used_texture_mask
        shader=Shader(entry['hash'],entry['stage'],texture_mask,entry['specialization_constants_mask'],outputs,flags,entry['source_name'],attrs,primary,late)
        shaders.append(shader)
        records.append({'hash':key,'stage':entry['stage'],'source':entry['source_name'],
                        'source_sha256':hashlib.sha256(entry['source'].read_bytes()).hexdigest(),
                        'activation':entry['activation'],'pipeline_pair_id':entry['pipeline_pair_id'],
                        'counterpart_hashes':entry['counterpart_hashes'],
                        'support_radius_bits':entry['support_radius_bits'],
                        'supported_sample_count_mask':entry['supported_sample_count_mask'],
                        'used_texture_mask':texture_mask,'output_mask':outputs,'late':bool(late)})
        print(f"compiled override {key} {entry['source_name']}",flush=True)
    archive=build_archive(shaders);parse_archive(archive)
    _atomic_write(out,archive)
    receipt={'compiled':len(shaders),'manifest_sha256':hashlib.sha256(original).hexdigest(),'archive_sha256':hashlib.sha256(archive).hexdigest(),'records':records}
    _atomic_write(work/'manifest.json',(json.dumps(receipt,indent=2)+'\n').encode())
    lines=['#pragma once','#include <cstdint>','#include <array>','namespace rex::graphics::gta4_metal {',
           'struct OverridePolicy { uint64_t hash; uint32_t stage, activation, pair, mask, radius; uint64_t counterpart; };',
           f'inline constexpr std::array<OverridePolicy, {len(records)}> kOverridePolicies = {{{{']
    for record in records:
        counterparts=record['counterpart_hashes']
        if len(counterparts)>1:raise ValueError('Extend policy schema for multiple counterparts before publishing')
        counterpart=counterparts[0] if counterparts else 0
        lines.append('    {0x'+record['hash']+'ull, '+str(record['stage'])+'u, '+str(record['activation'])+'u, '+str(record['pipeline_pair_id'])+'u, '+str(record['supported_sample_count_mask'])+'u, '+str(record['support_radius_bits'])+'u, '+hex(counterpart)+'ull},')
    lines += ['}};','} // namespace rex::graphics::gta4_metal','']
    _atomic_write(out.with_name('override_metadata.h'),'\n'.join(lines).encode())
    return receipt


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--work',type=Path,required=True)
    args=parser.parse_args()
    result=compile_all(args.repo.resolve(),args.output.resolve(),args.work.resolve())
    print(json.dumps({k:v for k,v in result.items() if k!='records'},indent=2))
    return 0

if __name__=='__main__':
    raise SystemExit(main())
