#!/usr/bin/env python3
"""Compile real retail/lifted PPC comparisons using the configured app toolchain."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parents[2]
HELPERS = ('__savegprlr_14', '__restgprlr_14', '__savegprlr_26',
           '__restgprlr_26', '__savefpr_27', '__restfpr_27')

def extract(text, name):
    marker = 'DEFINE_REX_FUNC(' + name + ') {'
    start = text.index(marker)
    end = text.find('\nDEFINE_REX_FUNC(', start + 1)
    return text[start:end] if end != -1 else text[start:]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, default=WORKSPACE/'out/build/macos-release')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--lifecycle', action='store_true', help='Test production request/state/trace ownership with isolated guest callbacks')
    parser.add_argument('--runtime-library', type=Path, default=ROOT.parent/'out/mac-arm64/librexruntime.dylib')
    args = parser.parse_args()
    args.output_dir = args.output_dir.resolve()
    args.build_dir = args.build_dir.resolve()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location('streaming_derivation', ROOT/'tools/derive_streaming_hooks.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    reference, identities = [], {}
    helper_source = (ROOT/'generated/gta4_recomp.78.cpp').read_text()
    for name in HELPERS:
        body = extract(helper_source, name)
        reference.append(body)
        identities[name] = hashlib.sha256(body.encode()).hexdigest()
    for name, unit, digest in module.SPECS:
        body = module.original(name, unit, digest)
        body = body.replace('DEFINE_REX_FUNC(' + name + ') {',
                            'extern "C" void __imp__' + name + '(PPCContext& ctx, uint8_t* base) {', 1)
        reference.append(body)
        identities[name] = digest
    header = args.output_dir/'streaming_retail_reference.inc'
    header.write_text('// Test-only exact generated PPC reference and ABI helpers.\n' + '\n'.join(reference))
    (args.output_dir/'reference-identities.json').write_text(json.dumps(identities, indent=2))
    target = 'glue/gta4-recomp/CMakeFiles/LibertyRecomp.dir/src/gta4_streaming_diagnostics.cpp.o'
    query = subprocess.run(['ninja', '-C', str(args.build_dir), '-t', 'commands', target],
                           check=True, capture_output=True, text=True)
    commands = [line for line in query.stdout.splitlines()
                if ' -c ' in line and line.endswith('/src/gta4_streaming_diagnostics.cpp')]
    if len(commands) != 1: raise SystemExit('Cannot identify the configured app compile command')
    original = shlex.split(commands[0])
    if Path(original[0]).name == 'ccache': original.pop(0)
    command = []
    skip = False
    for index, item in enumerate(original):
        if skip: skip = False; continue
        if item in ('-o', '-MT', '-MF', '-c'):
            skip = True; continue
        if item in ('-MD', '-MMD'): continue
        command.append('-O1' if item == '-O3' else item)
    command.extend(['-g', '-I' + str(args.output_dir), '-Wno-asm-operand-widths'])
    if args.sanitize: command.extend(['-fsanitize=address,undefined', '-fno-omit-frame-pointer'])
    executable = args.output_dir/('streaming_lifecycle' if args.lifecycle else 'streaming_guest_equivalence')
    if args.lifecycle:
        module.fixture_sources(module.originals(), args.output_dir)
        library = args.runtime_library.resolve()
        if not library.is_file(): raise SystemExit(f'Runtime library missing: {library}')
        command.extend([str(WORKSPACE/'tools/tests/gta4_streaming_lifecycle.cpp'), str(library),
                        '-Wl,-rpath,' + str(library.parent),
                        '-L/opt/homebrew/opt/llvm/lib/c++', '-Wl,-rpath,/opt/homebrew/opt/llvm/lib/c++'])
    else:
        command.append(str(ROOT.parent/'tests/unit/system/streaming_guest_equivalence_test.cpp'))
    command.extend(['-o', str(executable)])
    result = subprocess.run(command, cwd=args.build_dir, capture_output=True, text=True, timeout=90)
    (args.output_dir/'compile.log').write_text(result.stdout + result.stderr)
    if result.returncode:
        print(result.stdout + result.stderr)
        raise SystemExit(result.returncode)
    env = os.environ.copy()
    if args.sanitize:
        env['ASAN_OPTIONS'] = 'detect_leaks=1:halt_on_error=1'
        env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
    result = subprocess.run([str(executable)], cwd=args.output_dir, env=env,
                            capture_output=True, text=True, timeout=60)
    (args.output_dir/'run.log').write_text(result.stdout + result.stderr)
    (args.output_dir/'result.json').write_text(json.dumps({
        'exit_code': result.returncode, 'command': command,
        'sanitizers': 'address,undefined,leaks' if args.sanitize else '',
        'stdout': result.stdout, 'stderr': result.stderr,
        'scope': 'Actual PPC bodies and ABI helpers; synthetic guest memory and external module callbacks',
    }, indent=2))
    print(result.stdout + result.stderr)
    raise SystemExit(result.returncode)

if __name__ == '__main__':
    main()
