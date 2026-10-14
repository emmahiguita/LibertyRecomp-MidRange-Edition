#!/usr/bin/env python3
"""Compile the renderer's existing utility and presentation shaders for Metal.

No runtime compiler or Vulkan dependency. Generated MSL and a manifest preserve
source provenance and the exact shader interfaces for tests and GPU inspection.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile


def run(args: list[str]) -> str:
    result = subprocess.run(args, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=180)
    if result.returncode:
        raise RuntimeError(f"Tool failed ({result.returncode}): {' '.join(args)}\n{result.stdout}")
    return result.stdout


def write_changed(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_bytes() == data:
        return
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(data)
    try:
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def header_spirv(path: Path) -> bytes:
    text = path.read_text()
    arrays = re.findall(r'(?:const|constexpr)\s+(?:unsigned\s+int|uint32_t)\s+\w+\s*\[\s*\]\s*=\s*\{(.*?)\};', text, re.S)
    payloads = []
    for array in arrays:
        values = [int(word, 16) for word in re.findall(r'0[xX]([0-9a-fA-F]+)(?:[uU])?', array)]
        if values and values[0] == 0x07230203:
            payloads.append(struct.pack(f'<{len(values)}I', *values))
    if len(payloads) != 1:
        raise ValueError(f'{path}: expected one SPIR-V array, found {len(payloads)}')
    return payloads[0]


def build(repo: Path, output: Path, work: Path, deployment: str,
          cross: str, glslang: str) -> dict:
    sdk = repo/'glue/rexglue-sdk-main'
    native = sdk/'src/graphics/gta4_native'
    ui = sdk/'src/ui/shaders/vulkan_spirv'
    metal = run(['xcrun', '--find', 'metal']).strip()
    linker = run(['xcrun', '--find', 'metallib']).strip()
    sdk_path = run(['xcrun', '--sdk', 'macosx', '--show-sdk-path']).strip()
    versions = run([metal, '--version']) + run([cross, '--revision'])
    jobs: list[tuple[str, Path, str, tuple[str, ...]]] = []
    for source in sorted(ui.glob('guest_output_*_ps.h')):
        jobs.append((source.stem, source, 'frag', ()))
    for source in sorted(native.glob('*.glsl')):
        jobs.append((source.stem, source, 'frag', ()))
    jobs.append(('hdr_bilinear_present_ps', native/'hdr_present_ps.glsl', 'frag', ('GTA4_FUSED_BILINEAR_HDR=1',)))
    for name in ('resolve_convert_ps', 'resolve_convert_msaa_ps'):
        jobs.append((name+'_hdr', native/(name+'.glsl'), 'frag', ('GTA4_RESOLVE_HDR_MIRROR=1',)))
    for quality in ('LOW', 'MEDIUM', 'HIGH', 'ULTRA'):
        for source in sorted((native/'smaa').glob('*.glsl')):
            jobs.append((source.stem+'_'+quality.lower(), source, 'frag', ('SMAA_PRESET_'+quality+'=1',)))
    if not jobs:
        raise ValueError('No existing renderer shaders were found')
    work.mkdir(parents=True, exist_ok=True)
    records = []
    air_files = []
    smaa_include = repo/'LibertyRecompLib/postfx/smaa'
    for name, source, stage, defines in jobs:
        spv = work/(name+'.spv')
        msl = work/(name+'.metal')
        air = work/(name+'.air')
        stamp = work/(name+'.recipe')
        recipe = (versions+sdk_path+deployment+repr(defines)).encode()+source.read_bytes()+Path(__file__).read_bytes()
        if source.parent.name == 'smaa':
            recipe += (smaa_include/'SMAA.hlsl').read_bytes()
        fingerprint = hashlib.sha256(recipe).hexdigest()
        if not (air.exists() and msl.exists() and stamp.exists() and stamp.read_text() == fingerprint):
            if source.suffix == '.h':
                write_changed(spv, header_spirv(source))
            else:
                command = [glslang, '-V', '--target-env', 'vulkan1.0', '-S', stage, '-g0', '-Os',
                           '-I'+str(source.parent), '-I'+str(smaa_include), '-o', str(spv), str(source)]
                for define in defines:
                    command += ['--define-macro', define]
                run(command)
            function = 'liberty_'+name
            run([cross, str(spv), '--msl', '--msl-version', '30000',
                 '--msl-decoration-binding', '--msl-pad-fragment-output',
                 '--rename-entry-point', 'main', function, stage, '--output', str(msl)])
            # The existing SPIR-V is the arithmetic reference. Do not relax NaN
            # comparisons or silently introduce fast-math changes in this step.
            run([metal, '-c', str(msl), '-o', str(air), '-isysroot', sdk_path,
                 '-std=metal3.0', '-fno-fast-math', '-Wno-unused-variable',
                 '-mmacosx-version-min='+deployment])
            write_changed(stamp, fingerprint.encode())
        records.append({'name': name, 'function': 'liberty_'+name,
                        'source': str(source.relative_to(repo)), 'defines': defines,
                        'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                        'msl_sha256': hashlib.sha256(msl.read_bytes()).hexdigest()})
        air_files.append(str(air))
        print('compiled pass', name, flush=True)
    with tempfile.TemporaryDirectory(dir=work) as temporary:
        candidate = Path(temporary)/'passes.metallib'
        run([linker, *air_files, '-o', str(candidate)])
        data = candidate.read_bytes()
        if not data.startswith(b'MTLB'):
            raise ValueError('Metal linker output is not a library')
        write_changed(output, data)
    result = {'functions': records, 'library_sha256': hashlib.sha256(output.read_bytes()).hexdigest()}
    write_changed(work/'manifest.json', (json.dumps(result, indent=2)+'\n').encode())
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--deployment-target', default='26.0')
    parser.add_argument('--spirv-cross', default=shutil.which('spirv-cross'))
    parser.add_argument('--glslang', default=shutil.which('glslangValidator'))
    args = parser.parse_args()
    if not args.spirv_cross or not args.glslang:
        parser.error('Existing shader compilation requires spirv-cross and glslangValidator')
    report = build(args.repo.resolve(), args.output.resolve(), args.work.resolve(),
                   args.deployment_target, args.spirv_cross, args.glslang)
    print(json.dumps({'compiled': len(report['functions']), 'library_sha256': report['library_sha256']}))


if __name__ == '__main__':
    main()
