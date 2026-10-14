#!/usr/bin/env python3
"""Build tiny, deterministic shader fixtures for the production title draw path.

This does not alter the shipped stock or override shader archives.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "metal"))
from shader_archive import Attribute, MetalCompiler, Shader, VERTEX, PIXEL, build_archive

COMMON = """
#include <metal_stdlib>
using namespace metal;
struct Varying {
    float4 position [[position]];
    float4 color [[user(TEXCOORD0)]];
    float2 uv [[user(TEXCOORD1)]];
};
"""
VERTEX_SOURCE = COMMON + """
struct Input {
    float4 position [[attribute(0)]];
    float4 color [[attribute(1)]];
    float2 uv [[attribute(2)]];
};
vertex Varying shaderMain(Input input [[stage_in]]) {
    return {input.position, input.color, input.uv};
}
"""
PIXEL_SOURCE = COMMON + """
struct Push {
    constant float4* vertex_constants;
    constant float4* pixel;
    constant uint* shared;
};
fragment float4 shaderMain(Varying input [[stage_in]], constant Push& push [[buffer(8)]]) {
    return float4(input.uv, input.color.x, 1.0f) * push.pixel[0];
}
"""

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    compiler = MetalCompiler(output / "compiler", "macosx", "26.0")
    vertex = Shader(1, VERTEX, 0, 0, 0, 0, "geometry_vertex", (
        Attribute(0, 0, 0, 4), Attribute(1, 17, 0, 4), Attribute(2, 13, 0, 2)
    ), compiler.compile(VERTEX_SOURCE))
    pixel = Shader(2, PIXEL, 0, 0, 1, 0, "geometry_pixel", (), compiler.compile(PIXEL_SOURCE))
    data = build_archive((vertex, pixel))
    for name in ("title_shader_archive.bin", "override_shader_archive.bin"):
        (output / name).write_bytes(data)
    print(f"geometry_fixture={output} shaders=2 archive_bytes={len(data)}")

if __name__ == "__main__":
    main()
