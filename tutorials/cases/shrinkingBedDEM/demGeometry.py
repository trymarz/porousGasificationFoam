"""DEM geometry shared by relax.py and MPI_lambda.py (shrinkingBedDEM).

Walls are the blockMeshDict boundary (walls, outlet and the empty y-faces,
all imported as facets).

The packing is a single hexagonal layer of spheres at y = 0 filling the
column up to bed_top, the top of the FVM bed in setFieldsDict, with
translation in y and rotation about x and z blocked, so the bed stays one
sphere thick.

The spheres are soft and heavily damped: they stand in for the FVM solid's
slow, gravity-driven rearrangement, not for real particle contact, and the
soft contacts allow a DEM step 10x larger than 4 mm wood would need.
"""
from math import radians, sqrt

from yade import *          # FrictMat
from yade import utils, ymport
from yade.system import O

sphere_radius = 0.004   # initial radius (m) -- 4 mm biomass pellets
half_depth    = 0.008   # mesh slab half-thickness in y (blockMeshDict)
bed_top       = 0.36    # m, top of the FVM bed (setFieldsDict)
blocked_dofs  = "yXZ"   # keep the bed in the xz-plane
dem_dt        = 5e-5    # DEM step (s); Allrun reads it for YADE_NSTEPS
damping       = 0.7     # NewtonIntegrator: quasi-static rearrangement
sphere_young  = 5e5     # Pa; dem_dt = 0.27 p-wave step, overlap under bed < 5% r

# Column outline in the xz-plane, counter-clockwise (blockMeshDict).
outline = [(-0.10, 0.00), (0.10, 0.00), (0.10, 0.40), (-0.10, 0.40)]


def add_materials():
    O.materials.append(FrictMat(young=sphere_young, poisson=0.5,
                                frictionAngle=radians(15), density=1050,
                                label="spheremat"))
    O.materials.append(FrictMat(young=sphere_young * 100, poisson=0.5,
                                frictionAngle=0, density=0, label="wallmat"))


def add_walls():
    O.bodies.append(ymport.blockMeshDict("system/blockMeshDict",
                                         material="wallmat"))


def _inside(x, z):
    hit = False
    n = len(outline)
    for i in range(n):
        (x1, z1), (x2, z2) = outline[i], outline[(i + 1) % n]
        if (z1 > z) != (z2 > z) and x < x1 + (z - z1) * (x2 - x1) / (z2 - z1):
            hit = not hit
    return hit


def _wall_distance(x, z):
    d = float("inf")
    n = len(outline)
    for i in range(n):
        (x1, z1), (x2, z2) = outline[i], outline[(i + 1) % n]
        ex, ez = x2 - x1, z2 - z1
        t = max(0.0, min(1.0, ((x - x1) * ex + (z - z1) * ez) / (ex * ex + ez * ez)))
        d = min(d, sqrt((x - x1 - t * ex) ** 2 + (z - z1 - t * ez) ** 2))
    return d


def hex_layer():
    """Centres of a touching hexagonal layer inside the outline, below bed_top."""
    r = sphere_radius
    clearance = 1.01 * r
    x_min = min(p[0] for p in outline)
    x_max = max(p[0] for p in outline)
    centres = []
    row, z = 0, r * 1.01
    while z < bed_top - r:
        x = x_min + clearance + (r if row % 2 else 0.0)
        while x < x_max:
            if _inside(x, z) and _wall_distance(x, z) >= clearance:
                centres.append((x, 0.0, z))
            x += 2.0 * r
        row += 1
        z += sqrt(3.0) * r
    return centres
