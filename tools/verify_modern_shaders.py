#!/usr/bin/env python3
"""Verify modern shader assets and run the FusionFix sun-shaft numerical oracle.

--fixtures writes deterministic GPU inputs and double-precision reference outputs.
--compare compares one or more GPU output directories with that reference.
--update-header refreshes the embedded Vulkan sun shader from the canonical GLSL.
"""
from pathlib import Path
import argparse
import hashlib
import re
import shutil
import struct
import subprocess
import tempfile
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
NATIVE = ROOT / 'glue/rexglue-sdk-main/src/graphics/gta4_native'
REFERENCE = ROOT / 'LibertyRecompLib/postfx/fusion_modern_reference'


def compile_shader(update):
    with tempfile.TemporaryDirectory(prefix='liberty-modern-') as temp:
        spv = Path(temp) / 'sun.spv'
        subprocess.run([shutil.which('glslangValidator'), '-V', '--target-env', 'vulkan1.3',
                        '-S', 'frag', '-g0', '-Os', '-o', str(spv), str(NATIVE/'sun_shafts_ps.glsl')], check=True)
        subprocess.run([shutil.which('spirv-val'), '--target-env', 'vulkan1.3', str(spv)], check=True)
        data = spv.read_bytes()
        words = struct.unpack(f'<{len(data)//4}I', data)
        header = NATIVE/'sun_shafts_ps.h'
        if update:
            header.write_text('// Generated from sun_shafts_ps.glsl by tools/verify_modern_shaders.py.\n'
                              '#pragma once\n#include <cstdint>\nconst uint32_t gta4_native_sun_shafts_ps[] = {\n' +
                              ''.join('  '+', '.join(f'0x{x:08x}' for x in words[i:i+8])+',\n'
                                      for i in range(0, len(words), 8))+'};\n')
        assert list(words) == [int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]+)', header.read_text())], 'Stale Vulkan embedding'
        print('Sun shader SPIR-V:', hashlib.sha256(data).hexdigest())


def sample(image, uv, hardware=True):
    h, w = image.shape[:2]
    p = uv*np.array([w, h])-0.5
    lo = np.floor(p).astype(np.int64)
    f = p-lo
    # Apple GPU linear samplers (Metal and MoltenVK) use 8 fractional bits.
    # The cloud mask uses explicit shader loads and float interpolation instead.
    # Model this separately so the oracle does not mistake sampler precision
    # for a difference in the upstream radial-blur equations.
    if hardware:
        f = np.round(f*256)/256
    x0, y0 = np.clip(lo[..., 0], 0, w-1), np.clip(lo[..., 1], 0, h-1)
    x1, y1 = np.clip(lo[..., 0]+1, 0, w-1), np.clip(lo[..., 1]+1, 0, h-1)
    return ((image[y0, x0]*(1-f[..., 0, None])+image[y0, x1]*f[..., 0, None])*(1-f[..., 1, None]) +
            (image[y1, x0]*(1-f[..., 0, None])+image[y1, x1]*f[..., 0, None])*f[..., 1, None])


def uv_grid(w, h):
    y, x = np.mgrid[:h, :w]
    return np.stack(((x+0.5)/w, (y+0.5)/h), axis=-1)


def sun_reference(scene, depth, clouds, p):
    # Direct equations from vendored SunShafts_PS.hlsl: prepass, SSDraw twice, SSAdd.
    h, w = scene.shape[:2]
    uv = uv_grid(w//2, h//2)
    z = sample(depth, uv)[..., 0]
    near, far, fog = p[2, 1:]
    if fog:
        distance = near*far/(z*(far-near)+near)
        sky = np.clip(np.log2(distance/near)/np.log2(far/near)*10-9, 0, 1)
    else:
        sky = (z == 0).astype(float)
    ray = np.stack(((uv[..., 0]*2-1)/p[1, 3], (1-uv[..., 1]*2)/p[2, 0], -np.ones_like(z)), axis=-1)
    ray /= np.linalg.norm(ray, axis=-1, keepdims=True)
    sun = (np.sum(ray*(p[1, :3]/np.linalg.norm(p[1, :3])), axis=-1) >= 0.996).astype(float)
    edge = np.min(np.minimum(uv, 1-uv)*np.array([w/h, 1]), axis=-1)
    mask = sky*sun*np.clip(edge*32, 0, 1)*p[0, 2]*p[0, 3]*sample(clouds, uv, hardware=False)[..., 0]
    color = sample(scene, uv)[..., :3]*mask[..., None]
    for _ in range(2):
        cursor = uv.copy()
        delta = (uv-p[0, :2])*(0.9/24)
        accumulated = sample(color, cursor)
        decay = 1.0
        for _ in range(24):
            cursor -= delta
            accumulated += sample(color, cursor)*decay
            decay *= 0.95
        color = accumulated
    result = scene.copy()
    result[..., :3] += sample(color, uv_grid(w, h))
    return result.astype('<f4')


def fixtures(folder):
    folder.mkdir(parents=True, exist_ok=True)
    reference = folder/'reference'
    reference.mkdir(exist_ok=True)
    source = (REFERENCE/'SunShafts_PS.hlsl').read_text()
    assert '#define NUM_SAMPLES 24' in source and 'float illuminationDecay = 1.0f' in source
    cases = [(64, 64, 'uniform'), (97, 65, 'clouds'), (48, 720, 'depth'),
             (128, 72, 'opaque'), (128, 72, 'blocked'), (96, 64, 'edge'), (96, 64, 'no_fog')]
    for case, (w, h, kind) in enumerate(cases):
        rng = np.random.default_rng(case)
        scene = rng.uniform(0, 8, (h, w, 4)).astype('<f4')
        half = np.zeros((h//2, w//2, 4), '<f4')
        depth = np.zeros_like(scene)
        clouds = np.ones_like(scene)
        p = np.array([[.5, .5, .002, 1], [0, 0, -1, 1], [1, .1, 1000, 1]], '<f4')
        if kind == 'uniform':
            scene[:] = 1
            p[1, 3] = p[2, 0] = 20
        if kind == 'clouds': clouds[:] = rng.uniform(0, 1, (h, w, 1))
        if kind == 'depth':
            distance = np.linspace(50, 1000, w)[None, :]
            depth[..., 0] = (.1*1000/distance-.1)/(1000-.1)
        if kind == 'opaque': depth[..., 0] = 1
        if kind == 'blocked': clouds[:] = 0
        if kind == 'edge':
            p[0, 0] = 1.1
            p[1, :3] = [1.2, 0, -1]
        if kind == 'no_fog':
            p[2, 3] = 0
            depth[:, :w//2, 0] = .00001
        (folder/f'case{case}.input').write_bytes(struct.pack('<II', w, h)+p.tobytes()+
            b''.join(x.tobytes() for x in [scene, half, depth, clouds]))
        expected = sun_reference(scene.astype(float), depth.astype(float), clouds.astype(float), p.astype(float))
        (reference/f'case{case}.rgba32f').write_bytes(expected.tobytes())
        if kind in ('opaque', 'blocked'): assert np.array_equal(expected, scene)
        if kind == 'uniform':
            gain = (1+sum(.95**i for i in range(24)))**2
            assert abs(float(expected[h//2, w//2, 0])-(1+.002*gain)) < 1e-6
        print(f'fixture {case}: {w}x{h}, {kind}')


def compare(paths):
    reference, *outputs = paths
    files = sorted(reference.glob('*.rgba32f'))
    assert files and outputs
    for folder in outputs:
        for file in files:
            expected = np.fromfile(file, '<f4')
            result = np.fromfile(folder/file.name, '<f4')
            assert result.shape == expected.shape and np.isfinite(result).all()
            error = np.abs(result-expected)
            assert np.all(error <= 0.0001), (folder/file.name, error.max())
            print(f'{folder.name}/{file.stem}: max error {error.max():.8f}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--update-header', action='store_true')
    parser.add_argument('--fixtures', type=Path)
    parser.add_argument('--compare', nargs='+', type=Path)
    args = parser.parse_args()
    if args.compare: compare(args.compare)
    else:
        compile_shader(args.update_header)
        if args.fixtures: fixtures(args.fixtures)
