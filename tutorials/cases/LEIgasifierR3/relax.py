"""DEM-only relaxation for LEIgasifierR3: settle the hot bed before coupling.

Packs the bed up to hotBed.z_fill with hexagonal sphere rows, each row at
the hot-bed radius of its height, gives each sphere the hot-bed state there
(hotBed.py), lets it settle under gravity until the
unbalanced force is small, and writes the resting packing to
relaxedSpheres.txt (for MPI_lambda.py) and the per-sphere state to
bedState.txt (for hotBed.py's 0/ fields).

Usage, serial, from the case directory:
    yade -n -x relax.py
"""
import os
import sys
from math import sqrt

sys.path.insert(0, os.getcwd())

from yade import *
from yade import pack, utils
from yade.system import O

import demGeometry as geom
import hotBed

maxIters      = 200000   # hard cap on relaxation steps
checkInterval = 500      # steps between convergence checks
targetUnbal   = 0.01     # unbalanced force / mean contact force
targetVel     = 1e-3     # m/s, fastest sphere

O.dynDt = False
O.dt    = geom.dem_dt    # same DEM step as MPI_lambda.py

geom.add_materials()
geom.add_walls()
def graded_layer(z_max):
    """Hexagonal rows bottom-up, each at the hot-bed radius of its height."""
    xs = [p[0] for p in geom.outline]
    z, row, out = min(p[1] for p in geom.outline), 0, []
    r_prev = None
    while True:
        r = geom.sphere_radius * hotBed.lam(*hotBed.state(z)[:2])
        # Equal rows nest at sqrt(3) r; a radius step needs the full 2 r.
        z += 1.01 * r if r_prev is None else (
            sqrt(3.0) * r if abs(r - r_prev) < 1e-9 else 2.0 * max(r, r_prev))
        if z > z_max:
            return out
        x = min(xs) + 1.01 * r + (r if row % 2 else 0.0)
        while x < max(xs):
            if geom.inside(x, z) and geom.wall_distance(x, z) >= 1.01 * r:
                out.append(((x, 0.0, z), r))
            x += 2.0 * r
        row, r_prev = row + 1, r


state = {}
for c, r in graded_layer(hotBed.z_fill):
    b = utils.sphere(c, r, material="spheremat")
    b.state.blockedDOFs = geom.blocked_dofs
    state[O.bodies.append(b)] = hotBed.state(c[2])

O.engines = [
    ForceResetter(),
    InsertionSortCollider([Bo1_Sphere_Aabb(), Bo1_Facet_Aabb()],
                          label="collider"),
    InteractionLoop(
        [Ig2_Sphere_Sphere_ScGeom(), Ig2_Facet_Sphere_ScGeom()],
        [Ip2_FrictMat_FrictMat_FrictPhys()],
        [Law2_ScGeom_FrictPhys_CundallStrack()],
    ),
    NewtonIntegrator(gravity=(0, 0, -9.81), damping=geom.damping),
]
collider.verletDist = geom.sphere_radius * 0.2

nSpheres = sum(1 for b in O.bodies if isinstance(b.shape, Sphere))
print(f"[RELAX] {nSpheres} spheres, dt = {O.dt}")

def maxSphereVel():
    return max(b.state.vel.norm() for b in O.bodies
               if isinstance(b.shape, Sphere))

settled = False
while O.iter < maxIters:
    O.run(checkInterval, True)
    unbal, vmax = utils.unbalancedForce(), maxSphereVel()
    print(f"[RELAX] iter {O.iter}  t = {O.time:.4f} s  "
          f"unbalanced = {unbal:.4g}  max vel = {vmax:.3g} m/s")
    if unbal < targetUnbal and vmax < targetVel:
        settled = True
        break

if not settled:
    sys.exit(f"[RELAX] not settled after {maxIters} steps")

sp = pack.SpherePack()
sp.fromSimulation()
sp.save("relaxedSpheres.txt")
with open("bedState.txt", "w") as f:
    f.write("# x z radius M X Ts   (M moisture left, X wood converted)\n")
    for i, (M, X, T) in sorted(state.items()):
        p, r = O.bodies[i].state.pos, O.bodies[i].shape.radius
        f.write("%.6f %.6f %.6e %.6f %.6f %.3f\n" % (p[0], p[2], r, M, X, T))
print(f"[RELAX] wrote relaxedSpheres.txt and bedState.txt ({len(sp)} spheres)")
