#!/usr/bin/env python3
"""Check production Vulkan profiling labels on/off against identical GPU pixels.

macOS-only standalone test. Does not launch the game or modify its bundle/config.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--compiler', type=Path)
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('This standalone fixture requires the macOS Vulkan bundle.')
    root = Path(__file__).resolve().parents[1]
    sdk = root / 'glue/rexglue-sdk-main'
    app = (args.app or root / 'out/build/macos-release/LibertyRecomp/Liberty Recompiled.app').resolve()
    source = root / 'tools/tests/native_gpu_profile_labels_smoke.cpp'
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    compiler = args.compiler
    if compiler is None:
        compiler = next((p for p in (Path('/opt/homebrew/opt/llvm/bin/clang++'),
                                   Path('/usr/local/opt/llvm/bin/clang++')) if p.is_file()), None)
    if compiler is None:
        parser.error('Pass --compiler with the LLVM C++ compiler used for this build.')
    compiler = compiler.expanduser().absolute()
    libcxx = compiler.parent.parent / 'lib/c++'
    vulkan = app / 'Contents/Resources/vulkan'
    icd = vulkan / 'share/vulkan/icd.d/MoltenVK_icd.json'
    runtime = app / 'Contents/MacOS/librexruntime.dylib'
    for path in (compiler, source, runtime, icd):
        if not path.is_file():
            parser.error(f'Missing required file: {path}')
    sysroot = subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip()
    binary = output / 'native-gpu-profile-labels-smoke'
    command = [str(compiler), '--driver-mode=g++', '-std=c++23', '-O2', '-DFMT_HEADER_ONLY']
    command += ['-I' + str(p) for p in (sdk/'src', sdk/'include', sdk/'thirdparty/fmt/include', sdk/'thirdparty/vulkan-headers/include')]
    command += [str(source), '-L'+str(app/'Contents/MacOS'), '-lrexruntime']
    command += ['-Wl,-rpath,'+str(p) for p in (app/'Contents/MacOS', app/'Contents/Frameworks')]
    if libcxx.is_dir():
        command += ['-L'+str(libcxx), '-Wl,-rpath,'+str(libcxx)]
    command += ['-mmacosx-version-min=26.0', '-isysroot', sysroot, '-o', str(binary)]
    record = {'source_sha256':digest(source), 'runtime_sha256':digest(runtime), 'command':command, 'cases':[]}
    def save() -> None:
        (output/'results.json').write_text(json.dumps(record, indent=2))
    with (output/'compile.log').open('w') as log:
        process = subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT, timeout=90)
    record['compile_exit'] = process.returncode
    save()
    if process.returncode:
        print('Compilation failed; see '+str(output/'compile.log'), file=sys.stderr)
        return process.returncode
    environment = dict(os.environ, VK_ICD_FILENAMES=str(icd), VULKAN_SDK=str(vulkan),
                       DYLD_LIBRARY_PATH=':'.join(str(p) for p in (app/'Contents/MacOS', app/'Contents/Frameworks', vulkan/'lib')))
    for key in ('REX_GPU_FLIGHT_TRACE_PATH','MTL_CAPTURE_ENABLED','MTLCAPTURE_WAIT_FOR_SIGNAL'):
        environment.pop(key, None)
    for mode in ('0','1'):
        pixels = output / ('labels-'+mode+'.rgba')
        with (output/('labels-'+mode+'.log')).open('w') as log:
            process = subprocess.run([str(binary), str(pixels)], cwd=root,
                env=dict(environment, REX_GTA4_GPU_PASS_MARKERS=mode), stdout=log,
                stderr=subprocess.STDOUT, timeout=30)
        case = {'labels':mode=='1', 'exit_code':process.returncode}
        if process.returncode == 0:
            case['pixels_sha256'] = digest(pixels)
        record['cases'].append(case)
        save()
        if process.returncode:
            return 1
    record['identical_pixels'] = len({c['pixels_sha256'] for c in record['cases']}) == 1
    record['runtime_unchanged'] = digest(runtime) == record['runtime_sha256']
    save()
    print(json.dumps(record, indent=2))
    return 0 if record['identical_pixels'] and record['runtime_unchanged'] else 1


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, subprocess.SubprocessError) as exc:
        print(str(exc), file=sys.stderr)
        raise SystemExit(1)
