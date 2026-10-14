#!/usr/bin/env python3
"""GPU regression: the actual Metal sky override must preserve stock rasterization.

Uses the packaged shader libraries, an asymmetric sky triangle, both windings,
all cull modes, and the title's half-pixel offset. No game instance is launched.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import struct
import subprocess

import shader_archive


def prepare(stock: Path, modern: Path, work: Path) -> None:
    work.mkdir(parents=True, exist_ok=True)
    for name, path in (("stock", stock), ("modern", modern)):
        shaders = shader_archive.parse_archive(path.read_bytes())
        sky = next(s for s in shaders if s.shader_hash == 0x0421316FC8CF1313)
        assert sky.attributes == (
            shader_archive.Attribute(0, 0, 0, 4),
            shader_archive.Attribute(1, 13, 0, 4),
        )
        (work / f"{name}.metallib").write_bytes(sky.early)

    # Positive, unequal Y coordinates expose both a vertical inversion and the
    # resulting winding reversal. Keep the title's horizon deformation inactive.
    vertices = [(-0.75, 0.20, 0.5, 1, 0.15, 0.25, 0, 0),
                (0.65, 0.30, 0.5, 1, 0.85, 0.35, 0, 0),
                (-0.25, 0.90, 0.5, 1, 0.45, 0.90, 0, 0)]
    (work / "vertices.bin").write_bytes(b"".join(struct.pack("<8f", *v) for v in vertices))
    constants = bytearray(0x1000)

    def c(index: int, values: tuple) -> None:
        struct.pack_into("<4f", constants, index * 16, *values)

    for base in (0, 8):
        for row in range(4):
            c(base + row, tuple(float(column == row) for column in range(4)))
    c(64, (0, 1, 0, 0))  # sun direction
    c(65, (0.2, 0.4, 0.8, 0))  # nonblack sky
    c(68, (1, 0, 0, 0))
    c(69, (0, 0, 0, 1))
    c(72, (1, 1, 1, 1))
    c(73, (1, 0, 0, 0))
    (work / "constants.bin").write_bytes(constants)
    for label, offset in (("zero", (0, 0)), ("half", (-1 / 64, 1 / 64))):
        shared = bytearray(0x500)
        struct.pack_into("<2f", shared, 552, *offset)
        (work / f"shared-{label}.bin").write_bytes(shared)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stock", type=Path, required=True)
    parser.add_argument("--modern", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.stock, args.modern, args.work)
    root = Path(__file__).resolve().parents[2]
    binary = args.work.resolve() / "metal-sky-orientation-test"
    subprocess.run(["xcrun", "clang++", "-std=c++20", "-fobjc-arc", "-O2",
                    str(root / "tools/tests/metal_sky_orientation_gpu_test.mm"),
                    "-framework", "Foundation", "-framework", "Metal", "-o", str(binary)], check=True)
    subprocess.run([str(binary), str(args.work.resolve())], check=True)


if __name__ == "__main__":
    main()
