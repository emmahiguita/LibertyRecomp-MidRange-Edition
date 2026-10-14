#!/usr/bin/env python3
"""Compile the canonical DoF shader, verify its embedded Vulkan copy, and create GPU fixtures.

The numerical oracle follows the pinned FusionShaders assembly in
LibertyRecompLib/postfx/fusion_dof_reference. GPU runners consume identical inputs.
Usage: python3 tools/verify_dof_shaders.py --update-header --fixtures /tmp/dof
       python3 tools/verify_dof_shaders.py --compare /tmp/dof-reference /tmp/dof-metal /tmp/dof-vulkan
"""
from pathlib import Path
import argparse, hashlib, re, shutil, struct, subprocess, tempfile
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
NATIVE=ROOT/'glue/rexglue-sdk-main/src/graphics/gta4_native'
REFERENCE=ROOT/'LibertyRecompLib/postfx/fusion_dof_reference'

def fixtures(folder):
    folder.mkdir(parents=True,exist_ok=True)
    for case,(w,h,zero) in enumerate([(64,32,False),(48,720,False),(48,1080,False),(48,2160,False),(48,4320,False),(65,33,False),(64,32,True)]):
        rng=np.random.default_rng(case)
        scene=rng.uniform(0,4,(h,w,4)).astype(np.float32)
        # Deliberately independent half scene detects gathering the full/stipple scene by mistake.
        half=rng.uniform(0,8,(h//2,w//2,4)).astype(np.float32)
        depth=np.zeros_like(scene);mask=np.zeros_like(scene)
        p=np.array([[.1,1000,0,0],[4,2,80,12],[.8,.05,1.1,0]],np.float32)
        if zero:p[2,:3]=0
        z=np.tile(np.linspace(.1,250,w,dtype=np.float32),(h,1))
        depth[...,0]=(p[0,0]*p[0,1]/z-p[0,0])/(p[0,1]-p[0,0])
        mask[...,3]=rng.choice([0,.125,.5,1],(h,w))
        path=folder/f'case{case}.input'
        path.write_bytes(struct.pack('<II',w,h)+p.tobytes()+b''.join(x.tobytes() for x in [scene,half,depth,mask]))
        print(f'fixture {case}: {w}x{h} zero_dof={zero}')

def compile_shader(update):
    with tempfile.TemporaryDirectory(prefix='liberty-dof-') as temporary:
        spv=Path(temporary)/'dof.spv'
        subprocess.run([shutil.which('glslangValidator'),'-V','--target-env','vulkan1.3','-S','frag','-g0','-Os','-o',str(spv),str(NATIVE/'split_postfx_ps.glsl')],check=True)
        subprocess.run([shutil.which('spirv-val'),'--target-env','vulkan1.3',str(spv)],check=True)
        data=spv.read_bytes();words=struct.unpack(f'<{len(data)//4}I',data)
        header=NATIVE/'split_postfx_ps.h'
        if update:
            header.write_text('// Generated from split_postfx_ps.glsl by tools/verify_dof_shaders.py --update-header.\n#pragma once\n#include <cstdint>\nconst uint32_t gta4_native_split_postfx_ps[] = {\n'+''.join('  '+', '.join(f'0x{x:08x}' for x in words[i:i+8])+',\n' for i in range(0,len(words),8))+'};\n')
        embedded=[int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)',header.read_text())]
        assert list(words)==embedded,'Embedded Vulkan shader is stale; run --update-header'
        print('Canonical shader and Vulkan embedding match:',hashlib.sha256(data).hexdigest())

def compare(reference, outputs):
    cases=sorted(reference.glob('*.rgba32f'))
    assert cases and outputs, 'Supply the GPU reference directory and at least one backend output'
    for directory in outputs:
        for expected in cases:
            actual=directory/expected.name
            a=np.fromfile(actual,dtype='<f4');b=np.fromfile(expected,dtype='<f4')
            assert a.shape==b.shape and np.isfinite(a).all() and np.isfinite(b).all(),actual
            delta=np.abs(a-b)
            # Includes reciprocal/log/pow round-trip for the PC depth encoding.
            assert np.all(delta<=np.float32(.00002)),f'{actual}: max error {delta.max()}'
            print(f'{directory.name}/{expected.stem}: max error {delta.max():.8f}')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--update-header',action='store_true');parser.add_argument('--fixtures',type=Path);parser.add_argument('--compare',nargs='+',type=Path)
    args=parser.parse_args()
    if args.compare:compare(args.compare[0],args.compare[1:])
    else:
        compile_shader(args.update_header)
        if args.fixtures:fixtures(args.fixtures)
