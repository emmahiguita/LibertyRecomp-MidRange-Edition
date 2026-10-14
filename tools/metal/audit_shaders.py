#!/usr/bin/env python3
"""Inventory existing shader assets and frontend consumers; never rewrite shaders."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys


def load(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    if not spec or not spec.loader:
        raise ValueError(f"Cannot import {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def audit(root: Path) -> dict:
    validator = load(root / 'tools/validate_shader_cache_candidate.py', 'metal_audit_ranges')
    cache = root / 'LibertyRecompLib/shader/shader_cache.cpp'
    text = cache.read_text()
    entries = validator.parse_entries(text, cache)
    manifest_path = root / 'LibertyRecompLib/shader_overrides/manifest.json'
    manifest = json.loads(manifest_path.read_text())
    stock = {entry.shader_hash: entry for entry in entries}
    override_rows = []
    for record in manifest['overrides']:
        source = root / 'LibertyRecompLib/shader_overrides' / record['source']
        shader_hash = int(record['hash'], 0)
        override_rows.append({**record, 'source_exists': source.is_file(),
            'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest() if source.is_file() else None,
            'has_stock_entry': shader_hash in stock,
            'activation_policy': 'modern_shader_policy.h / shader_override_policy.h',
            'metal_runtime_wired': False})
    source_roots = ['LibertyRecomp/gpu/shader', 'glue/rexglue-sdk-main/src/graphics/gta4_native',
                    'glue/rexglue-sdk-main/src/ui/shaders', 'glue/rexglue-sdk-main/src/ui/metal']
    rows = []
    for directory in source_roots:
        for source in sorted((root / directory).rglob('*')):
            if not source.is_file() or source.suffix not in {'.hlsl', '.glsl', '.metal'}:
                continue
            row = {'source': str(source.relative_to(root)),
                   'sha256': hashlib.sha256(source.read_bytes()).hexdigest(), 'compiled_counterparts': []}
            alternatives = [source.with_suffix(source.suffix + '.metallib'), source.with_suffix('.metallib'),
                            source.with_suffix(source.suffix + '.spv'), source.with_suffix('.spv')]
            if source.suffix == '.hlsl':
                alternatives.extend([root/'LibertyRecomp/gpu/shader/msl'/f'{source.stem}.metal',
                                     root/'LibertyRecomp/gpu/shader/msl'/f'{source.stem}.metal.metallib'])
            row['compiled_counterparts'] = sorted({str(p.relative_to(root)) for p in alternatives if p.is_file()})
            row['active_use_requires_callsite_verification'] = True
            rows.append(row)
    frontend = root / 'glue/rexglue-sdk-main/gta4-recomp/src/gta4_frontend_hooks.cpp'
    settings = sorted(set(re.findall(r'Setting\{\s*"LR_[^"]+",\s*TextId::\w+,\s*"([^"]+)"', frontend.read_text())))
    consumers = []
    search_roots = [root/'glue/rexglue-sdk-main/gta4-recomp/src', root/'glue/rexglue-sdk-main/src/graphics',
                    root/'glue/rexglue-sdk-main/src/ui']
    source_files = [p for d in search_roots for p in d.rglob('*') if p.suffix in {'.cpp', '.h', '.mm'}]
    for setting in settings:
        hits = []
        for source in source_files:
            for line_number, line in enumerate(source.read_text(errors='replace').splitlines(), 1):
                if setting in line:
                    hits.append({'file': str(source.relative_to(root)), 'line': line_number, 'text': line.strip()})
        consumers.append({'setting': setting, 'references': hits})
    return {'schema': 1, 'stock_cache_sha256': hashlib.sha256(cache.read_bytes()).hexdigest(),
            'stock': {'entries': len(entries), 'metal_libraries': sum(bool(e.air_size) for e in entries),
                      'spirv_modules': sum(bool(e.spirv_size) for e in entries),
                      'late_spirv_variants': sum(bool(e.late_spirv_size) for e in entries)},
            'overrides': override_rows, 'utility_sources': rows, 'frontend_settings': consumers,
            'limitations': ['Asset existence does not establish runtime parity.',
                           'Compiled counterparts may be older than their source.',
                           'Metal utility and override bindings need GPU validation.']}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = audit(args.repo.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'stock': result['stock'], 'override_entries': len(result['overrides']),
                      'utility_sources': len(result['utility_sources']),
                      'frontend_settings': len(result['frontend_settings']), 'report': str(args.output)}, indent=2))
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
