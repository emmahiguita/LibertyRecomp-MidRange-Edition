#!/usr/bin/env python3
"""Compile the existing validated override SPIR-V to Metal; preserve manifest policy."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import shader_archive as archive


def build(root: Path, cache: Path, compiler: Path, output: Path, work: Path, header: Path) -> dict:
    text = cache.read_text()
    records = re.findall(r'^\s*\{(0x[0-9A-Fa-f]+)ull,\s*kShaderOverrideStage(Pixel|Vertex),[^\n]*?\b(kShaderOverrideSpirv\d+),\s*sizeof\([^)]*\),\s*(kShaderOverrideLateSpirv\d+|nullptr),[^\n]*?"([^"\n]+)"\},', text, re.M)
    manifest = json.loads((root/'LibertyRecompLib/shader_overrides/manifest.json').read_text())
    expected = {(int(row['hash'], 0), row['stage']): row for row in manifest['overrides']}
    if {(int(h, 0), stage.lower()) for h, stage, *_ in records} != set(expected):
        raise ValueError('compiled overrides and manifest have different inventories')
    ranges = archive._load_module(root/'tools/validate_shader_cache_candidate.py', 'metal_override_stock')
    stock_path = root/'LibertyRecompLib/shader/shader_cache.cpp'
    stock = {e.shader_hash: e for e in ranges.parse_entries(stock_path.read_text(), stock_path)}
    work.mkdir(parents=True, exist_ok=True)
    metal = archive.MetalCompiler(work/'compiled', 'macosx', '26.0')
    entries, evidence = [], []
    declarations = ['#pragma once', '#include <array>', '#include "shader_override_policy.h"',
                    'namespace rex::graphics::gta4_metal {',
                    'inline gta4_native::ShaderOverrideCandidate FindOverrideCandidate(uint64_t hash) {',
                    '  using namespace gta4_native;', '  switch (hash) {']
    for raw_hash, stage, early_name, late_name, filename in records:
        shader_hash = int(raw_hash, 0)
        row = expected[(shader_hash, stage.lower())]
        variants, metadata = [], []
        for variant, symbol in [('early', early_name), ('late', late_name)]:
            if symbol == 'nullptr':
                continue
            match = re.search(r'constexpr uint32_t '+re.escape(symbol)+r'\[\] = \{(.*?)\};', text, re.S)
            if not match:
                raise ValueError(f'missing override words: {symbol}')
            words = [int(value, 16) for value in re.findall(r'0x([0-9A-Fa-f]+)u', match.group(1))]
            binary = struct.pack(f'<{len(words)}I', *words)
            spv = work/f'{shader_hash:016X}.{variant}.spv'
            msl = spv.with_suffix('.metal')
            meta = spv.with_suffix('.json')
            archive._atomic_write(spv, binary)
            subprocess.run([str(compiler.resolve()), str(spv), str(msl), str(meta)], check=True, timeout=60)
            info = json.loads(meta.read_text())
            if info['stage'] != (archive.PIXEL if stage == 'Pixel' else archive.VERTEX):
                raise ValueError('override stage changed during translation')
            if variant == 'late' and info['early']:
                raise ValueError('late override forces early fragment tests')
            # Stock Metal libraries use the original HLSL varying semantics.
            # SPIRV-Cross emits locnN; align only interface annotations, not math.
            source = msl.read_text()
            def varying(match):
                location = int(match.group(1))
                if location < 16:
                    name = f'TEXCOORD{location}'
                elif location < 18:
                    name = f'COLOR{location - 16}'
                else:
                    raise ValueError('override varying is outside the shared interface')
                return f'[[user({name})]]'
            source = re.sub(r'\[\[user\(locn(\d+)\)\]\]', varying, source)
            archive._atomic_write(msl, source.encode())
            variants.append(metal.compile(source))
            metadata.append(info)
            evidence.append({'hash': f'{shader_hash:016X}', 'variant': variant,
                             'spirv_sha256': hashlib.sha256(binary).hexdigest(),
                             'metal_sha256': hashlib.sha256(variants[-1]).hexdigest()})
        info = metadata[0]
        if len(metadata) > 1 and any(metadata[1][k] != info[k] for k in ('outputs','depth','attributes','coverage')):
            raise ValueError('override variants have different interfaces')
        flags = (archive.FLAG_NATIVE_OUTPUT | (archive.FLAG_EARLY_TESTS if info['early'] else 0) |
                 (archive.FLAG_COVERAGE if info['coverage'] else 0) |
                 (archive.FLAG_LATE_VARIANT if len(variants) > 1 else 0)) if stage == 'Pixel' else 0
        texture_mask = int(row.get('used_texture_mask', '0xFFFFFFFF'), 0)
        if texture_mask == 0xFFFFFFFF:
            if shader_hash not in stock:
                raise ValueError(f'{raw_hash}: override lacks explicit texture metadata and stock entry')
            texture_mask = stock[shader_hash].used_texture_mask
        attrs = tuple(archive.Attribute(index, semantic, numeric, count) for semantic,index,numeric,count in info['attributes'])
        entries.append(archive.Shader(shader_hash, info['stage'], texture_mask,
            int(row['specialization_constants_mask'], 0), info['outputs'] | (16 if info['depth'] else 0),
            flags, filename, attrs, variants[0], variants[1] if len(variants)>1 else b''))
        counterparts = [int(v, 0) for v in row.get('counterpart_hashes', [])]
        mask = 0
        for value in row.get('supported_sample_counts', []): mask |= value
        declarations += [f'    case {raw_hash}ull: {{',
            f'      static constexpr std::array<uint64_t, {len(counterparts)}> counterparts{{'+','.join(f'0x{v:016X}ull' for v in counterparts)+'};',
            '      return {.present=true, .activation=ShaderOverrideActivation::'+('kPipelinePair' if row.get('activation')=='pipeline_pair' else 'kStage')+',',
            f'        .pipeline_pair_id={row.get("pipeline_pair_id",0)}, .shader_hash=hash,',
            f'        .support_radius_bits={row.get("support_radius_bits","0")}u, .supported_sample_count_mask={mask},',
            '        .counterpart_hashes=counterparts};', '    }']
        print(f'compiled override {shader_hash:016X} {filename}', flush=True)
    declarations += ['    default: return {.shader_hash=hash};', '  }', '}', '}']
    data = archive.build_archive(entries)
    archive._atomic_write(output, data)
    archive._atomic_write(header, ('\n'.join(declarations)+'\n').encode())
    report = {'entries': len(entries), 'variants': len(evidence), 'sources': evidence,
              'source_cache_sha256': hashlib.sha256(cache.read_bytes()).hexdigest(),
              'archive_sha256': hashlib.sha256(data).hexdigest()}
    archive._atomic_write(work/'manifest.json', (json.dumps(report, indent=2)+'\n').encode())
    return report

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
    for name in ('cache','compiler','output','work','header'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    result = build(args.repo.resolve(), args.cache, args.compiler, args.output, args.work, args.header)
    print(json.dumps({k:v for k,v in result.items() if k!='sources'}, indent=2))
