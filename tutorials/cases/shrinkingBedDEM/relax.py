"""DEM-only relaxation for shrinkingBedDEM: settle the bed before coupling.

Packs the column with a hexagonal sphere layer (demGeometry.py),
lets it settle under gravity until the unbalanced force is small, and
writes the resting packing to relaxedSpheres.txt for MPI_lambda.py.

Usage, serial, from the case directory:
    yade -n -x relax.py
"""
import os
import sys

sys.path.insert(0, os.getcwd())

from yade import *
from yade import pack, utils
from yade.system import O

import demGeometry as geom

maxIters      = 200000   # hard cap on relaxation steps
checkInterval = 500      # steps between convergence checks
targetUnbal   = 0.01     # unbalanced force / mean contact force
targetVel     = 1e-3     # m/s, fastest sphere

O.dynDt = False
O.dt    = geom.dem_dt    # same DEM step as MPI_lambda.py

geom.add_materials()
geom.add_walls()
for c in geom.hex_layer():
    b = utils.sphere(c, geom.sphere_radius, material="spheremat")
    b.state.blockedDOFs = geom.blocked_dofs
    O.bodies.append(b)

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
print(f"[RELAX] wrote relaxedSpheres.txt ({len(sp)} spheres)")
