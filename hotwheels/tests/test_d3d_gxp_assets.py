#!/usr/bin/env python3
"""Ensure shipped D3D GXP blobs are embedded byte-for-byte in the Vita build header."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
subprocess.run(
    ["python3", str(ROOT / "tools/embed_d3d_gxp.py"), "--check"],
    cwd=ROOT,
    check=True,
)
