"""DEM geometry shared by relax.py, MPI_lambda.py and checkDemWalls.py.

Walls are every boundary face of system/blockMeshDict (walls, gas ports and
the empty slab faces, imported as facets) plus the grate, which only the
spheres see. All of it comes from leiGeometry.py, the mesh's own table.

The packing is a single hexagonal layer of spheres at y = 0 filling the bed
region above the grate, with translation in y and rotation about x and z
blocked, so the bed stays one sphere thick. Soft, heavily damped contacts as
in LEIgasifierDEM: they stand in for slow rearrangement, not real contact.
"""
from math import radians, sqrt

import leiGeometry as lei

sphere_radius = 0.004   # 8 mm softwood pellets (Striugas et al. 2014, Table 1)
half_depth    = lei.half_depth
blocked_dofs  = "yXZ"   # keep the bed in the xz-plane
dem_dt        = 5e-5    # DEM step (s); Allrun reads it for YADE_NSTEPS
damping       = 0.7     # NewtonIntegrator: quasi-static rearrangement
sphere_young  = 5e5     # Pa; dem_dt = 0.27 p-wave step, as LEIgasifierDEM

outline = lei.bed_outline


def add_materials():
    from yade import FrictMat
    from yade.system import O
    O.materials.append(FrictMat(young=sphere_young, poisson=0.5,
                                frictionAngle=radians(15), density=1050,
                                label="spheremat"))
    O.materials.append(FrictMat(young=sphere_young * 100, poisson=0.5,
                                frictionAngle=0, density=0, label="wallmat"))


def grate_facets():
    """Two triangles spanning the chamber floor at z_grate, across the slab."""
    (x0, z), (x1, _) = lei.grate
    h = half_depth
    a, b, c, d = (x0, -h, z), (x1, -h, z), (x1, h, z), (x0, h, z)
    return [(a, b, c), (a, c, d)]


def add_walls():
    from yade import utils, ymport
    from yade.system import O
    O.bodies.append(ymport.blockMeshDict("system/blockMeshDict",
                                         material="wallmat"))
    O.bodies.append([utils.facet(list(t), material="wallmat")
                     for t in grate_facets()])


def inside(x, z):
    hit = False
    n = len(outline)
    for i in range(n):
        (x1, z1), (x2, z2) = outline[i], outline[(i + 1) % n]
        if (z1 > z) != (z2 > z) and x < x1 + (z - z1) * (x2 - x1) / (z2 - z1):
            hit = not hit
    return hit


def wall_distance(x, z):
    d = float("inf")
    n = len(outline)
    for i in range(n):
        (x1, z1), (x2, z2) = outline[i], outline[(i + 1) % n]
        ex, ez = x2 - x1, z2 - z1
        t = max(0.0, min(1.0, ((x - x1) * ex + (z - z1) * ez) / (ex * ex + ez * ez)))
        d = min(d, sqrt((x - x1 - t * ex) ** 2 + (z - z1 - t * ez) ** 2))
    return d


def hex_layer():
    """Centres of a touching hexagonal layer that fits inside the bed region."""
    r = sphere_radius
    clearance = 1.01 * r
    xs = [p[0] for p in outline]
    zs = [p[1] for p in outline]
    centres = []
    row, z = 0, min(zs) + clearance
    while z < max(zs) - clearance:
        x = min(xs) + clearance + (r if row % 2 else 0.0)
        while x < max(xs):
            if inside(x, z) and wall_distance(x, z) >= clearance:
                centres.append((x, 0.0, z))
            x += 2.0 * r
        row += 1
        z += sqrt(3.0) * r
    return centres
