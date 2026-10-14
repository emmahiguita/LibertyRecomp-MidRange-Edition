#!/usr/bin/env python3
"""Compile three real title shader variants for metal_temporal_motion_gpu_test.

Only the requested fixture directory is written. Shipped archives and the
shared CMake build tree are never modified. Honor the caller's TOOLCHAINS.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "metal"))
import shader_archive as archive
import temporal_shader_variants as temporal


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sources", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    compiler = None if args.prepare_only else archive.MetalCompiler(output / "compiler", "macosx", "26.0")
    manifest = []
    for name, shader_hash, stage, variant in (
        ("vertex", "672D97058AC7790B", archive.VERTEX, "early"),
        ("pixel", "D72B5CF469E02C54", archive.PIXEL, "early"),
        ("pixel-late", "D72B5CF469E02C54", archive.PIXEL, "late"),
    ):
        source_path = (args.sources / f"{shader_hash}.{variant}.metal").resolve()
        source = source_path.read_text()
        transformed = temporal.transform(source, stage)
        (output / f"{name}.metal").write_text(transformed)
        record = {
            "name": name, "shader_hash": shader_hash, "stage": stage,
            "variant": variant, "source": str(source_path),
            "source_sha256": hashlib.sha256(source.encode()).hexdigest(),
            "temporal_source_sha256": hashlib.sha256(transformed.encode()).hexdigest(),
        }
        if compiler:
            binary = compiler.compile(transformed)
            (output / f"{name}.metallib").write_bytes(binary)
            record["metallib_sha256"] = hashlib.sha256(binary).hexdigest()
        manifest.append(record)
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"temporal_motion_fixture={output} shaders={len(manifest)} compiled={compiler is not None}")


if __name__ == "__main__":
    main()
