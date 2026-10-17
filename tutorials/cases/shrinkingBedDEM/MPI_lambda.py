"""Yade DEM + porousGasificationFoam coupling script for shrinkingBedDEM.

Twin-domain case (issue #104): both coupling channels are live --
  - lambdaDot (OpenFOAM -> Yade): per-FVM-cell apparent-volume factor r
    (lambdaMode apparentVolume, constant/lambdaDict). FoamCoupling multiplies
    each body's state.lambda_ by r once per exchange; this script only reads
    lambda_ back to set the sphere's radius (radius = lambda_ * initial
    radius, see changeRadius() below).
  - Us (Yade -> OpenFOAM): FoamCoupling maps sphere velocities onto solid
    cells every step (isGaussianInterp), same mechanism as the reverse
    lambdaDot mapping. No Python code needed for this side.

The spheres start from relaxedSpheres.txt, written by relax.py: one
hexagonal layer filling the bed, settled under gravity before coupling, so
the run starts at rest. Only spheres in the reactive layer see r < 1; the
inert bed's cells have no chemical volume change, so r = 1 there.

Usage (2 Yade ranks pre-launched, 2 OpenFOAM ranks spawned by FoamCoupling):
    mpirun -n 2 yade MPI_lambda.py
"""
import os
import sys

sys.path.insert(0, os.getcwd())

from yade import mpy as mp
from yade import export, pack
from yade.system import O

import demGeometry as geom

comm = mp.comm_slave   # communicator for worker ranks

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
parallelYade       = True
numProcOF          = 2          # OpenFOAM MPI ranks

writeInterval      = 0.25       # VTK write interval (virtual seconds)
radiusInterval     = 0.01       # changeRadius() period (virtual seconds)
yadeDtFixed        = geom.dem_dt
O.dynDt            = False
O.dt               = yadeDtFixed

timeRatio = max(1, int(round(writeInterval / yadeDtFixed)))
NSTEPS    = int(os.environ.get('YADE_NSTEPS', 2000000))

if mp.rank == 0:
    print(f"[EXPORT] yadeDt={yadeDtFixed}  writeInterval={writeInterval}  iterPeriod={timeRatio}")

sphere_radius = geom.sphere_radius   # initial radius (m)

# ---------------------------------------------------------------------------
# Materials, walls and the relaxed packing (relax.py)
# ---------------------------------------------------------------------------
geom.add_materials()
geom.add_walls()

sp = pack.SpherePack()
sp.load("relaxedSpheres.txt")
for c, r in sp:
    b = sphere(c, r, material="spheremat")
    b.state.blockedDOFs = geom.blocked_dofs
    O.bodies.append(b)

sphereIDs = [b.id for b in O.bodies if type(b.shape) == Sphere]
os.makedirs("spheres", exist_ok=True)

if mp.rank == 0:
    print(f"[DEM] loaded {len(sphereIDs)} relaxed spheres")

# ---------------------------------------------------------------------------
# Fluid coupling
# ---------------------------------------------------------------------------
fluidCoupling = FoamCoupling()
fluidCoupling.couplingModeParallel = parallelYade
fluidCoupling.isGaussianInterp     = True
fluidCoupling.SetOpenFoamSolver("porousGasificationFoam", numProcOF)
fluidCoupling.setIdList(sphereIDs)
fluidCoupling.setNumParticles(len(sphereIDs))

# ---------------------------------------------------------------------------
# Radius shrinkage driven by lambda
# ---------------------------------------------------------------------------
def changeRadius():
    # lambda_ is the dimensionless size factor FoamCoupling keeps per body
    # (multiplied by r at each exchange), so radius = sphere_radius * lambda_.
    for i in sphereIDs:
        if i >= len(O.bodies) or O.bodies[i] is None:
            continue
        b = O.bodies[i]
        new_rad = sphere_radius * b.state.lambda_
        if new_rad < 1e-4:
            fluidCoupling.eraseId(b.id)
            mp.bodyErase(b.id)
        else:
            b.shape.radius = new_rad

# ---------------------------------------------------------------------------
# VTK sphere output
# ---------------------------------------------------------------------------
pvd_spheres    = []
sphere_frame   = 0

def export_spheres():
    global sphere_frame
    local_centers = []
    local_radii   = []
    local_vels    = []
    for b in O.bodies:
        if isinstance(b.shape, Sphere):
            local_centers.append(b.state.pos)
            local_radii.append(b.shape.radius)
            local_vels.append(b.state.vel)

    all_centers = mp.comm.gather(local_centers, root=0)
    all_radii   = mp.comm.gather(local_radii,   root=0)
    all_vels    = mp.comm.gather(local_vels,    root=0)

    if mp.rank == 0:
        centers = [p for sub in all_centers for p in sub]
        radii   = [r for sub in all_radii   for r in sub]
        vels    = [v for sub in all_vels    for v in sub]
        npts    = len(centers)

        fp = f"spheres/spheres_{sphere_frame:.1f}.vtp"
        with open(fp, "w") as f:
            f.write('<?xml version="1.0"?>\n')
            f.write('<VTKFile type="PolyData" version="0.1" byte_order="LittleEndian">\n')
            f.write('  <PolyData>\n')
            f.write(f'    <Piece NumberOfPoints="{npts}" NumberOfVerts="{npts}">\n')
            f.write('      <Points>\n')
            f.write('        <DataArray type="Float32" NumberOfComponents="3" format="ascii">\n')
            for p in centers:
                f.write(f'          {p[0]} {p[1]} {p[2]}\n')
            f.write('        </DataArray>\n      </Points>\n')
            f.write('      <Verts>\n')
            f.write('        <DataArray type="Int32" Name="connectivity" format="ascii">\n')
            for i in range(npts):
                f.write(f'          {i}\n')
            f.write('        </DataArray>\n')
            f.write('        <DataArray type="Int32" Name="offsets" format="ascii">\n')
            for i in range(npts):
                f.write(f'          {i+1}\n')
            f.write('        </DataArray>\n      </Verts>\n')
            f.write('      <PointData>\n')
            f.write('        <DataArray type="Float32" Name="radius" format="ascii">\n')
            for r in radii:
                f.write(f'          {r}\n')
            f.write('        </DataArray>\n')
            f.write('        <DataArray type="Float32" Name="velocity" NumberOfComponents="3" format="ascii">\n')
            for v in vels:
                f.write(f'          {v[0]} {v[1]} {v[2]}\n')
            f.write('        </DataArray>\n      </PointData>\n')
            f.write('    </Piece>\n  </PolyData>\n</VTKFile>\n')

        pvd_spheres.append((float(sphere_frame), os.path.basename(fp)))
        with open("spheres/spheres.pvd", "w") as f:
            f.write('<?xml version="1.0"?>\n<VTKFile type="Collection" version="0.1">\n  <Collection>\n')
            for t, fn in pvd_spheres:
                f.write(f'    <DataSet timestep="{t:.6f}" file="{fn}"/>\n')
            f.write('  </Collection>\n</VTKFile>\n')

        sphere_frame += float(writeInterval)

# ---------------------------------------------------------------------------
# Time-step diagnostics
# ---------------------------------------------------------------------------
def printAndSaveDtInfo():
    if mp.rank != 0:
        return
    yadeDt = O.dt
    foamDt = fluidCoupling.foamDeltaT
    ratio  = foamDt / yadeDt if yadeDt > 0 else float("inf")
    write_header = not os.path.exists("dtInfo.txt")
    with open("dtInfo.txt", "a") as f:
        if write_header:
            f.write("iter time yadeDt foamDt ratio\n")
        f.write(f"{O.iter} {O.time:.6e} {yadeDt:.6e} {foamDt:.6e} {ratio:.6f}\n")

# ---------------------------------------------------------------------------
# Engines
# ---------------------------------------------------------------------------
O.engines = [
    ForceResetter(),
    InsertionSortCollider(
        [Bo1_Sphere_Aabb(), Bo1_Facet_Aabb()],
        label="collider",
    ),
    InteractionLoop(
        [Ig2_Sphere_Sphere_ScGeom(), Ig2_Facet_Sphere_ScGeom()],
        [Ip2_FrictMat_FrictMat_FrictPhys()],
        [Law2_ScGeom_FrictPhys_CundallStrack()],
    ),
    GlobalStiffnessTimeStepper(
        timestepSafetyCoefficient=0.7,
        defaultDt=yadeDtFixed,
        timeStepUpdateInterval=50,
        parallelMode=True,
        label="ts",
    ),
    fluidCoupling,
    NewtonIntegrator(gravity=(0, 0, -9.81), damping=geom.damping, label="newton"),
    PyRunner(command="changeRadius()",        virtPeriod=radiusInterval),
    PyRunner(command="export_spheres()",      iterPeriod=timeRatio, firstIterRun=1),
    PyRunner(command="printAndSaveDtInfo()",  iterPeriod=timeRatio, firstIterRun=1),
]

collider.verletDist = sphere_radius * 0.2

# ---------------------------------------------------------------------------
# MPI run
# ---------------------------------------------------------------------------
mp.FLUID_COUPLING       = True
mp.DOMAIN_DECOMPOSITION = True
mp.YADE_TIMING          = False
mp.VERBOSE_OUTPUT       = False
mp.USE_CPP_INTERS       = False
mp.ERASE_REMOTE_MASTER  = True
mp.REALLOC_FREQUENCY    = 12
mp.fluidBodies          = sphereIDs
mp.mpirun(NSTEPS, np=numProcOF)
mp.mprint("shrinkingBedDEM run finished")

exit()
