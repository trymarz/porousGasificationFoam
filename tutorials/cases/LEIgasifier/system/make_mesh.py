"""Generate blockMeshDict for the 2D LEI downdraft gasifier tutorial.

Run from the case root or from system/:
    python3 system/make_mesh.py
    python3 make_mesh.py

Requires classy_blocks >= 1.9.6  (available via utilities/py_utils venv).
Writes system/blockMeshDict.
"""
from pathlib import Path

import classy_blocks as cb
import numpy as np

_here = Path(__file__).resolve().parent
# _here is the directory containing this script.
# If the script lives in system/, case_root is one level up.
case_root = _here.parent if _here.name == "system" else _here
out_path = case_root / "system" / "blockMeshDict"

# ---------------------------------------------------------------------------
# Gasifier geometry  (metres)
# Full 2D cross-section: X ∈ [−0.20, +0.20], Z ∈ [0, 1.00]
#
# Block corners in the XZ face (y=0), extruded in +Y.
# Convention: [bottom-left, bottom-right, top-right, top-left]
#   block  Z range        X range
#   0      0.00→0.20     ±0.15           freeboard / reduction outlet
#   1      0.20→0.30     ±0.15→±0.08    constriction (combustion zone)
#   2      0.30→0.32     ±0.08           throat
#   3      0.32→0.42     ±0.08→±0.15    expansion
#   4      0.42→1.00     ±0.15           main upper (drying + pyrolysis)
#   5      0.42→1.00     +0.15→+0.20    outer shell, right
#   6      0.42→1.00     −0.20→−0.15    outer shell, left
# ---------------------------------------------------------------------------
x_pts = [
    [-0.15,  0.15,  0.15, -0.15],   # 0
    [-0.15,  0.15,  0.08, -0.08],   # 1
    [-0.08,  0.08,  0.08, -0.08],   # 2
    [-0.08,  0.08,  0.15, -0.15],   # 3
    [-0.15,  0.15,  0.15, -0.15],   # 4
    [ 0.15,  0.20,  0.20,  0.15],   # 5
    [-0.20, -0.15, -0.15, -0.20],   # 6
]
z_pts = [
    [0.00, 0.00, 0.20, 0.20],   # 0
    [0.20, 0.20, 0.30, 0.30],   # 1
    [0.30, 0.30, 0.32, 0.32],   # 2
    [0.32, 0.32, 0.42, 0.42],   # 3
    [0.42, 0.42, 1.00, 1.00],   # 4
    [0.42, 0.42, 1.00, 1.00],   # 5
    [0.42, 0.42, 1.00, 1.00],   # 6
]

# Y extrusion: 0.016 m, centred at y = 0
extr_len = 0.016

# ---------------------------------------------------------------------------
# Cell counts
#
# Direction 0 = X within the XZ face (v0→v1 direction)   → explicit count
# Direction 1 = Y (extrusion direction)                   → count=1 (2D)
# Direction 2 = Z within the XZ face (v1→v2 direction)   → start_size
#
# IMPORTANT: for tapered (non-rectangular) blocks the Z-wires have varying
# lengths.  Using explicit counts in direction 2 causes InconsistentGrading
# errors in classy_blocks because the grader enforces equal counts on all
# parallel wires.  Using start_size lets classy_blocks derive the count from
# the wire length, which correctly produces different counts for different
# lengths within a tapered block.
#
# Shared-face constraints still satisfied:
#   Blocks 0-4 share z-constant faces → X-counts must match (NX_MAIN = 20)
#   Blocks 4, 5, 6 share x-constant faces → Z start_sizes must match
# ---------------------------------------------------------------------------
NX_MAIN  = 10     # X cells for blocks 0-4 (0.30 m wide → 0.030 m/cell)
NX_SHELL =  2     # X cells for outer shells 5 and 6 (0.05 m)
SS_Z     = 0.030  # Z start_size for all blocks (~0.030 m/cell)

# ---------------------------------------------------------------------------
# Patch assignment per block
#   "front" → low-z face,  "back"  → high-z face
#   "left"  → low-x face,  "right" → high-x face
#   "top"/"bottom" → Y faces (always → "sides", empty)
# ---------------------------------------------------------------------------
patches = [
    # 0  freeboard: outlet at z=0, walls on ±x
    {"front": "outlet", "back": None,    "left": "wall", "right": "wall"},
    # 1  constriction: walls on outer ±x edges only
    {"front": None,     "back": None,    "left": "wall", "right": "wall"},
    # 2  throat: walls
    {"front": None,     "back": None,    "left": "wall", "right": "wall"},
    # 3  expansion: walls
    {"front": None,     "back": None,    "left": "wall", "right": "wall"},
    # 4  upper main: inlet at z=1.0, x-faces shared with blocks 5 & 6
    {"front": None,     "back": "inlet", "left": None,   "right": None},
    # 5  outer right: inlet at z=1.0, outer wall at x=+0.20
    {"front": None,     "back": "inlet", "left": None,   "right": "wall"},
    # 6  outer left: inlet at z=1.0, outer wall at x=−0.20
    {"front": None,     "back": "inlet", "left": "wall", "right": None},
]

# ---------------------------------------------------------------------------
# Build mesh
# ---------------------------------------------------------------------------
mesh = cb.Mesh()

for i in range(len(x_pts)):
    face_pts = [
        [x_pts[i][0], 0.0, z_pts[i][0]],
        [x_pts[i][1], 0.0, z_pts[i][1]],
        [x_pts[i][2], 0.0, z_pts[i][2]],
        [x_pts[i][3], 0.0, z_pts[i][3]],
    ]

    box = cb.Extrude(cb.Face(face_pts), extr_len)
    box.translate([0.0, -extr_len * 0.5, 0.0])

    nx = NX_MAIN if i < 5 else NX_SHELL
    box.chop(0, count=nx)       # X direction (v0→v1)
    box.chop(1, start_size=SS_Z)  # Z direction (v1→v2, face 2nd axis)
    box.chop(2, count=1)        # Y direction (extrusion, single cell for 2D)

    box.set_patch("top",    "sides")
    box.set_patch("bottom", "sides")

    for face_name, patch_name in patches[i].items():
        if patch_name is not None:
            box.set_patch(face_name, patch_name)

    mesh.add(box)

mesh.modify_patch("outlet", "patch")
mesh.modify_patch("inlet",  "patch")
mesh.modify_patch("wall",   "wall")
mesh.modify_patch("sides",  "empty")

mesh.write(str(out_path))
print(f"Wrote {out_path}")
