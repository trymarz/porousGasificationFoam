"""DEM geometry shared by relax.py and MPI_lambda.py (LEIgasifierDEM).

Walls are the blockMeshDict boundary (walls, inlet, outlet and the empty
y-faces, all imported as facets) plus the two ledges under the outer
shells: those faces are blockMesh defaultFaces, not in the boundary list.

The packing is a single hexagonal layer of spheres at y = 0 filling the
whole gasifier outline (blocks 0-6), with translation in y and rotation
about x and z blocked, so the bed stays one sphere thick.
"""
from math import radians, sqrt

from yade import *          # FrictMat
from yade import utils, ymport
from yade.system import O

sphere_radius = 0.004   # initial radius (m) -- 4 mm biomass pellets
half_depth    = 0.008   # mesh slab half-thickness in y (make_mesh.py)
blocked_dofs  = "yXZ"   # keep the bed in the xz-plane

# Gasifier outline in the xz-plane, counter-clockwise (make_mesh.py blocks).
outline = [
    (-0.15, 0.00), (0.15, 0.00), (0.15, 0.20), (0.08, 0.30), (0.08, 0.32),
    (0.15, 0.42), (0.20, 0.42), (0.20, 1.00), (-0.20, 1.00), (-0.20, 0.42),
    (-0.15, 0.42), (-0.08, 0.32), (-0.08, 0.30), (-0.15, 0.20),
]


def add_materials():
    O.materials.append(FrictMat(young=25e6, poisson=0.5,
                                frictionAngle=radians(15), density=1050,
                                label="spheremat"))
    O.materials.append(FrictMat(young=25e6 * 100, poisson=0.5,
                                frictionAngle=0, density=0, label="wallmat"))


def add_walls():
    O.bodies.append(ymport.blockMeshDict("system/blockMeshDict",
                                         material="wallmat"))
    for x0, x1 in ((0.15, 0.20), (-0.20, -0.15)):
        a = (x0, -half_depth, 0.42)
        b = (x1, -half_depth, 0.42)
        c = (x1, half_depth, 0.42)
        d = (x0, half_depth, 0.42)
        O.bodies.append([utils.facet([a, b, c], material="wallmat"),
                         utils.facet([a, c, d], material="wallmat")])


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
    """Centres of a touching hexagonal layer that fits inside the outline."""
    r = sphere_radius
    clearance = 1.01 * r
    centres = []
    row, z = 0, r * 1.01
    while z < 1.0 - clearance:
        x = -0.20 + clearance + (r if row % 2 else 0.0)
        while x < 0.20:
            if _inside(x, z) and _wall_distance(x, z) >= clearance:
                centres.append((x, 0.0, z))
            x += 2.0 * r
        row += 1
        z += sqrt(3.0) * r
    return centres
