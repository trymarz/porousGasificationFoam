import os
from yade import mpy as mp
from yade import pack, utils

counter = [1]   # VTK frame counter (list for mutable closure capture)

# ── coupling / run control ──────────────────────────────────────────
parallelYade = True
numProcOF    = 2
nsteps       = int(3e6)   # ~3s headroom at YADE dt~1e-6 (OF stops first at endTime=2s)

# Sphere-VTK cadence: one frame every 0.05 s of sim time, matching the
# OpenFOAM writeInterval (40 frames over the 2 s run).
SAVE_VTK_VIRT_PERIOD  = 0.05

# ── materials ─────────────────────────────────────────────────────
O.materials.append(FrictMat(
    young=25e6, poisson=0.5, frictionAngle=0.2618,
    density=650, label='spheremat'))
O.materials.append(FrictMat(
    young=25e8, poisson=0.5, frictionAngle=0,
    density=0, label='wallmat'))

# ── walls (match the blockMesh domain: 0.04 x 0.04 x 0.24 m) ────────
# Floor at z=0 keeps the bed stacked; the column is short so settling is
# minimal. Side walls confine the particles to the cross-section.
O.bodies.append(utils.wall(position=0,    axis=2, sense=1,  material='wallmat')) # floor (inlet)
O.bodies.append(utils.wall(position=0.24, axis=2, sense=-1, material='wallmat')) # ceiling (outlet)
O.bodies.append(utils.wall(position=0,    axis=0, sense=1,  material='wallmat')) # xMin
O.bodies.append(utils.wall(position=0.04, axis=0, sense=-1, material='wallmat')) # xMax
O.bodies.append(utils.wall(position=0,    axis=1, sense=1,  material='wallmat')) # yMin
O.bodies.append(utils.wall(position=0.04, axis=1, sense=-1, material='wallmat')) # yMax

# ── particle bed ──────────────────────────────────────────────────
# Pack the spheres into the lower 5 mesh layers (z = 0 -> 0.15), aligned with
# the porous bed set by setFields. O.run() settling is not viable in MPI mode,
# so the bed is packed dense from the start via makeCloud.
numSpheres       = 120
radius           = 0.003    # < cell width (0.01), one sphere fits inside a cell
CHAR_CORE_RADIUS = 0.0015   # half the initial radius (pyrolysis char core)

mn = (0.005, 0.005, radius)
mx = (0.035, 0.035, 0.14)

sp = pack.SpherePack()
sp.makeCloud(mn, mx, rMean=radius, rRelFuzz=0.05, num=numSpheres)
O.bodies.append([sphere(c, r, material='spheremat') for c, r in sp])

sphereIDs = [b.id for b in O.bodies if isinstance(b.shape, Sphere)]
print(f"[YADE] Created {len(sphereIDs)} spheres, z range [{mn[2]:.4f}, {mx[2]:.4f}]")

# Initialize lambdaDot=1.0 on all spheres BEFORE coupling.
# YADE defaults lambdaDot to 0.0 (core/State.hpp), which causes
# changeRadius() to clamp every sphere to CHAR_CORE_RADIUS on the
# first PyRunner invocation (before FoamCoupling has set real values).
# Once clamped, max(new_rad, CHAR_CORE_RADIUS) keeps them stuck.
# Setting lambdaDot=1.0 preserves the as-created radius until
# FoamCoupling updates it from the Ts-driven lambdaDot field.
for b in O.bodies:
    if isinstance(b.shape, Sphere):
        b.state.lambdaDot = 1.0

os.makedirs("spheres", exist_ok=True)

# ── foam coupling ─────────────────────────────────────────────────
fluidCoupling = FoamCoupling()
fluidCoupling.couplingModeParallel = parallelYade
fluidCoupling.isGaussianInterp = False

fluidCoupling.SetOpenFoamSolver("porousGasificationFoam", numProcOF)
fluidCoupling.setIdList(sphereIDs)

# ── particle shrinkage ────────────────────────────────────────────
# Particles shrink toward a char core as the local solid heats up; the shrink
# factor lambdaDot is computed per-cell from Ts by lambdaDotModel on the OF
# side and pushed onto each particle by FoamCoupling. Once CHAR_CORE_RADIUS is
# reached, shrinkage stops and the particle is NOT erased: erasing a body
# mid-run is unsafe (FoamCoupling::setHydroForce() dereferences a stale id list
# -> null deref -> SIGSEGV). Clamping keeps every body alive.
#
# Fallback shrink rate: if FoamCoupling never updates lambdaDot (e.g. a cell
# with no particles, or Ts <= Tref so lambdaDot stays 1.0), changeRadius leaves
# the particle untouched (lambdaDot == 1.0 path). A tiny timer-based fallback is
# kept for robustness but is effectively disabled here because the Ts-driven
# field does the work.
SHRINK_FRAC = 0.0   # rely on Ts-driven lambdaDot; no blind timer shrink

def changeRadius():
    for b in O.bodies:
        if not isinstance(b.shape, Sphere):
            continue
        ld = b.state.lambdaDot
        # If FoamCoupling updated lambdaDot (<0.9999), use it (correct path)
        if ld < 0.9999:
            new_rad = b.shape.radius * ld
        else:
            # Fallback: lambdaDot still 1.0 (cold cell / no coupling update)
            new_rad = b.shape.radius * (1.0 - SHRINK_FRAC)
        b.shape.radius = max(new_rad, CHAR_CORE_RADIUS)

# ── VTK export ────────────────────────────────────────────────────
file_names_proc = []

def write_VTK_spheres():
    import vtk
    sphr = [b for b in O.bodies if isinstance(b.shape, Sphere)]
    sphr_cent = [b.state.pos   for b in sphr]
    sphr_radi = [b.shape.radius for b in sphr]
    sphr_vel  = [b.state.vel   for b in sphr]

    points = vtk.vtkPoints()
    for p in sphr_cent:
        points.InsertNextPoint(p)

    vertices = vtk.vtkCellArray()
    for i in range(len(sphr_cent)):
        vertices.InsertNextCell(1)
        vertices.InsertCellPoint(i)

    polydata = vtk.vtkPolyData()
    polydata.SetPoints(points)
    polydata.SetVerts(vertices)

    radi = vtk.vtkFloatArray()
    radi.SetName("radius")
    for r in sphr_radi:
        radi.InsertNextValue(r)
    polydata.GetPointData().SetScalars(radi)

    vel = vtk.vtkFloatArray()
    vel.SetNumberOfComponents(3)
    vel.SetName("velocity")
    for v in sphr_vel:
        vel.InsertNextTuple(v)
    polydata.GetPointData().SetVectors(vel)

    timeStamp = SAVE_VTK_VIRT_PERIOD * counter[0]
    counter[0] += 1
    rank = getattr(mp, 'rank', 0)
    vtp_name  = f"spheres-rank{rank}-{timeStamp:.4f}.vtp"
    fileName  = f"spheres/{vtp_name}"
    file_names_proc.append({"fileName": fileName, "timeStamp": timeStamp})

    writer = vtk.vtkXMLPolyDataWriter()
    writer.SetFileName(fileName)
    writer.SetInputData(polydata)
    writer.SetDataModeToAscii()
    writer.Write()

# ── engines ───────────────────────────────────────────────────────
O.engines = [
    ForceResetter(),
    InsertionSortCollider(
        [Bo1_Sphere_Aabb(), Bo1_Wall_Aabb()],
        label="collider",
    ),
    InteractionLoop(
        [Ig2_Sphere_Sphere_ScGeom(), Ig2_Wall_Sphere_ScGeom()],
        [Ip2_FrictMat_FrictMat_FrictPhys()],
        [Law2_ScGeom_FrictPhys_CundallStrack()],
    ),
    GlobalStiffnessTimeStepper(
        timestepSafetyCoefficient=0.5,
        defaultDt=1e-6,
        timeStepUpdateInterval=50,
        parallelMode=True,
        label="ts",
    ),
    fluidCoupling,
    NewtonIntegrator(gravity=(0, 0, -9.81), damping=0.3, label="newton"),
    # Apply the Ts-driven shrink factor frequently (every 1 ms of sim time) so
    # the radius decays smoothly: at the hottest cells lambdaDot ~ 0.9995, and
    # ~1400 calls bring r from 0.003 to the 0.0015 char core over the run.
    PyRunner(command="changeRadius()",
             virtPeriod=0.001,
             label="radiusChanger"),
    PyRunner(command="write_VTK_spheres()",
             virtPeriod=SAVE_VTK_VIRT_PERIOD),
]

# ── mpi launch ────────────────────────────────────────────────────
mp.YADE_TIMING            = False
mp.FLUID_COUPLING         = True
mp.VERBOSE_OUTPUT         = False
mp.USE_CPP_INTERS         = False
mp.ERASE_REMOTE_MASTER    = False
mp.ERASE_REMOTE           = False
mp.REALLOC_FREQUENCY      = 0
mp.fluidBodies            = sphereIDs
mp.DOMAIN_DECOMPOSITION   = True
mp.mpirun(nSteps=nsteps, np=numProcOF)
fluidCoupling.killMPI()
mp.mprint("Run finished")
exit()
