"""Yade DEM + porousGasificationFoam coupling script for LEIgasifier tutorial.

Spheres represent biomass/char particles in the active reaction zone
(throat + combustion zone, z=0.21→0.41 m).  OpenFOAM computes lambdaDot
for each sphere via the solid chemistry model; this script applies the
resulting radius shrinkage each step and removes particles that shrink
below the minimum threshold.

Usage (2 Yade ranks pre-launched, 2 OpenFOAM ranks spawned by FoamCoupling):
    mpirun -n 2 yade MPI_lambda.py
"""
import os
from yade import mpy as mp
from yade import export, pack, ymport
from yade.system import O

comm = mp.comm_slave   # communicator for worker ranks

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
parallelYade       = True
numProcOF          = 2          # OpenFOAM MPI ranks

writeInterval      = 0.1        # VTK write interval (virtual seconds)
yadeDtFixed        = 5e-6
O.dynDt            = False
O.dt               = yadeDtFixed

timeRatio = max(1, int(round(writeInterval / yadeDtFixed)))
NSTEPS    = int(os.environ.get('YADE_NSTEPS', 2000000))

if mp.rank == 0:
    print(f"[EXPORT] yadeDt={yadeDtFixed}  writeInterval={writeInterval}  iterPeriod={timeRatio}")

sphere_radius = 0.004   # initial radius (m) — 4 mm biomass pellets

# Active packing zone: throat + combustion section (z=0.21→0.41, X within ±0.07)
pack_box_lo = (-0.07, -0.006, 0.21)
pack_box_hi = ( 0.07,  0.006, 0.41)

# ---------------------------------------------------------------------------
# Materials
# ---------------------------------------------------------------------------
young   = 25e6
density = 1050   # wood/char (kg/m3, from yadeProperties)
O.materials.append(FrictMat(young=young, poisson=0.5, frictionAngle=radians(15),
                             density=density, label='spheremat'))
O.materials.append(FrictMat(young=young * 100, poisson=0.5, frictionAngle=0,
                             density=0, label='wallmat'))

# ---------------------------------------------------------------------------
# DEM boundary — gasifier walls from blockMeshDict
# ---------------------------------------------------------------------------
facets = ymport.blockMeshDict("system/blockMeshDict")
O.bodies.append(facets)

# ---------------------------------------------------------------------------
# Sphere packing — random packing inside the active zone
# ---------------------------------------------------------------------------
pred = pack.inAlignedBox(pack_box_lo, pack_box_hi)
sp = pack.SpherePack()
sp.makeCloud(
    Vector3(*pack_box_lo),
    Vector3(*pack_box_hi),
    rMean=sphere_radius,
    rRelFuzz=0.0,
)
O.bodies.append([sphere(c, r, material='spheremat') for c, r in sp])

sphereIDs = [b.id for b in O.bodies if type(b.shape) == Sphere]
os.makedirs("spheres", exist_ok=True)

if mp.rank == 0:
    print(f"[DEM] packed {len(sphereIDs)} spheres in zone {pack_box_lo} → {pack_box_hi}")

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
# Radius shrinkage driven by lambdaDot from OpenFOAM
# ---------------------------------------------------------------------------
def changeRadius():
    for b in [O.bodies[i] for i in sphereIDs if O.bodies[i] is not None]:
        new_rad = b.shape.radius * b.state.lambdaDot
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
    NewtonIntegrator(gravity=(0, 0, -9.81), damping=0.2, label="newton"),
    PyRunner(command="changeRadius()",        virtPeriod=writeInterval * 0.1),
    PyRunner(command="export_spheres()",      iterPeriod=timeRatio, firstIterRun=1),
    PyRunner(command="printAndSaveDtInfo()",  iterPeriod=timeRatio, firstIterRun=1),
]

collider.verletDist = sphere_radius * 0.2

# ---------------------------------------------------------------------------
# MPI run  — matches working DEM cases (DEM_UsInterp_solidU, MicroTGA-DEM)
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
mp.mprint("LEIgasifier run finished")

exit()
