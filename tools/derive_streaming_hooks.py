#!/usr/bin/env python3
"""Workspace entry point for the recompilation source-contract checker."""
from pathlib import Path
import runpy

if __name__ == '__main__':
    target = Path(__file__).resolve().parents[1] / 'glue/rexglue-sdk-main/gta4-recomp/tools/derive_streaming_hooks.py'
    runpy.run_path(str(target), run_name='__main__')
