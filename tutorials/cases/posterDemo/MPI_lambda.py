import os
from yade import mpy as mp
from yade import pack, utils

counter = [1]   # VTK frame counter (list for mutable closure capture)

# ── coupling / run control ──────────────────────────────────────────
parallelYade = True
numProcOF    = 2
nsteps       = int(3e6)   # ~3s headroom at YADE dt~1e-6 (OF stops first at endTime=2s)

# Sphere-VTK cadence: one frame every 0.01 s of sim time, matching the
# OpenFOAM writeInterval (200 frames over the 2 s run).
SAVE_VTK_VIRT_PERIOD  = 0.01

# ── materials ─────────────────────────────────────────────────────
O.materials.append(FrictMat(
    young=25e6, poisson=0.5, frictionAngle=0.2618,
    density=650, label='spheremat'))
O.materials.append(FrictMat(
    young=25e8, poisson=0.5, frictionAngle=0,
    density=0, label='wallmat'))

# ── walls (match the quasi-2D blockMesh domain: 0.04 x 0.004 x 0.096 m) ──
# Floor at z=0 and ceiling at z=0.096 confine the bed to the exact packed
# height (12 layers × 8 mm diameter) — spheres touch top and bottom walls
# so there is no room to settle. The only motion is shrinkage-driven.
# The y-walls at y=0 and y=0.004 (matching OpenFOAM's empty front/back
# faces) pin the spheres into a single monolayer at y=0.002.
O.bodies.append(utils.wall(position=0,     axis=2, sense=1,  material='wallmat')) # floor (inlet)
O.bodies.append(utils.wall(position=0.096, axis=2, sense=-1, material='wallmat')) # ceiling (outlet)
O.bodies.append(utils.wall(position=0,     axis=0, sense=1,  material='wallmat')) # xMin
O.bodies.append(utils.wall(position=0.04,  axis=0, sense=-1, material='wallmat')) # xMax
O.bodies.append(utils.wall(position=0,     axis=1, sense=1,  material='wallmat')) # yMin
O.bodies.append(utils.wall(position=0.004, axis=1, sense=-1, material='wallmat')) # yMax

# ── particle bed ──────────────────────────────────────────────────
# Regular 5×1×12 monolayer of touching spheres (r=0.004) fills the entire
# domain (z=0→0.097, 0.04 m wide) as a single layer at y=0.002 — the centre
# of the 1-cell-thick quasi-2D mesh. Spheres touch neighbours and all six
# walls → self-supporting, no settling. The only motion is shrinkage-driven
# rearrangement, producing a sustained Us field for solid-phase advection;
# porosity via porosityF independently.
#
# pack.regularOrtho places grid points at min+r+k·2r and pack.inAlignedBox
# admits a sphere only if it fits entirely (centre in [min+r, max-r]).
# So the x range [0,0.041] yields 5 centres (0.004…0.036) and z range
# [0,0.097] yields 12 (0.004…0.092). For y we want exactly one centre at
# 0.002: min+r = -0.002+0.004 = 0.002 gives the first grid point, and the
# admit window [0.002, max-r] = [0.002, 0.002] (max=0.006) keeps only it.
numSpheres       = 60      # 5×1×12 monolayer
radius           = 0.004   # diameter 8 mm (5 across 0.04 m)
CHAR_CORE_RADIUS = 0.001   # 25 % of initial (visible char core)

mn = (0,     -0.002, 0)
mx = (0.041,  0.007, 0.097)   # mx_z-r=0.093 > last-centre 0.092 → all 12 layers fit

sp = pack.regularOrtho(pack.inAlignedBox(mn, mx), radius, gap=0, material='spheremat')
O.bodies.append(sp)

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
# Spheres shrink linearly toward CHAR_CORE_RADIUS as the solid pyrolyses.
# lambdaDot = 1.0 − Ychar/Ychar_final (wood-remaining fraction, [0, 1]):
#   lambdaDot = 1.00  →  Ychar = 0.00  →  r = r₀ = 0.004 m
#   lambdaDot = 0.50  →  Ychar = 0.35  →  r = 0.0025 m
#   lambdaDot = 0.00  →  Ychar = 0.70  →  r = CHAR_CORE_RADIUS = 0.001 m
#
# The mapping is a linear interpolation:  r = r_core + (r₀ − r_core) × ld
#
# lambdaDot is computed per-cell on the OF side (lambdaDotModel::update)
# from the solid char mass fraction Ychar, pushed onto each particle by
# FoamCoupling, and applied here every 1 ms of sim time.  Once
# CHAR_CORE_RADIUS is reached, the particle is clamped (never erased —
# body erasure mid-run is unsafe).

# ── master←worker lambdaDot sync (MPI stopgap) ────────────────────
# In DOMAIN_DECOMPOSITION mode with ERASE_REMOTE_MASTER=False the YADE master
# (rank 0) keeps a full copy of every body, but FoamCoupling delivers the
# Ts-driven lambdaDot only to the worker that owns each body — the master's
# copies stay frozen at the initial radius. Mirror the worker's lambdaDot onto
# the master so rank-0 spheres shrink in step with the worker-owned ones.
#
# This is the immediately-testable counterpart to the C++ fix
# (FoamCoupling::syncLambdaDotToMaster): once a YADE built with that fix is in
# use this Python sync is redundant, but it stays harmless (idempotent).
#
# Collective + safe: changeRadius() runs on every rank at identical virtual
# time (parallel timestepper synchronises dt and iter), so the broadcast below
# is reached in lockstep on all ranks. Assumes the demo's single-worker
# topology (rank 1 owns the whole domain); for >1 worker, rely on the C++ fix.
def _sync_lambdaDot_master_slave():
    if not parallelYade:
        return
    comm = getattr(mp, 'comm', None)
    if comm is None:
        from mpi4py import MPI
        comm = MPI.COMM_WORLD
    if comm.Get_size() < 2:
        return
    rank = comm.Get_rank()
    # The worker (rank 1) owns the coupled bodies and receives lambdaDot from
    # FoamCoupling; broadcast its {id: lambdaDot} map to the master.
    ld_map = None
    if rank == 1:
        ld_map = {b.id: b.state.lambdaDot
                  for b in O.bodies if isinstance(b.shape, Sphere)}
    ld_map = comm.bcast(ld_map, root=1)
    if rank == 0 and ld_map:
        for b in O.bodies:
            if isinstance(b.shape, Sphere):
                ld = ld_map.get(b.id)
                if ld is not None:
                    b.state.lambdaDot = ld

def changeRadius():
    _sync_lambdaDot_master_slave()
    for b in O.bodies:
        if not isinstance(b.shape, Sphere):
            continue
        ld = b.state.lambdaDot
        # lambdaDot = 1 − Ychar/Ychar_final ∈ [0, 1]
        # Linearly interpolate radius between r₀ (all wood) and r_core (all char)
        new_rad = CHAR_CORE_RADIUS + (radius - CHAR_CORE_RADIUS) * ld
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
    # Apply the Ychar-driven shrink factor frequently (every 1 ms of sim time)
    # so the radius tracks the local char yield smoothly as chemistry advances.
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
