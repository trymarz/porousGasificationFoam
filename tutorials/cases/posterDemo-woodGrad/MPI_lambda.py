import os
from yade import mpy as mp
from yade import pack, utils

counter = [1]   # VTK frame counter (list for mutable closure capture)

# ── coupling / run control ──────────────────────────────────────────
# 2-proc MPI with manual decomposition: rank 0 owns z=0→0.088 m (all
# 270 spheres), rank 1 owns z=0.088→0.096 m (empty — no spheres).
# The MPI boundary sits 12 mm above the highest sphere (z=0.076),
# eliminating the boundary-coupling VTK flicker that affected the old
# even-split decomposition.
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

# ── walls (match the 2.5D blockMesh domain: 0.04 x 0.018 x 0.096 m) ──
# Floor at z=0 and ceiling at z=0.096 enclose the CFD domain — no gap.
# The y-walls at y=0 and y=0.018 confine three sphere layers (d=6 mm) with
# the outer spheres flush against the walls (0 mm clearance).
O.bodies.append(utils.wall(position=0,     axis=2, sense=1,  material='wallmat')) # floor   (inlet)
O.bodies.append(utils.wall(position=0.096, axis=2, sense=-1, material='wallmat')) # ceiling (outlet)
O.bodies.append(utils.wall(position=0,     axis=0, sense=1,  material='wallmat')) # xMin
O.bodies.append(utils.wall(position=0.04,  axis=0, sense=-1, material='wallmat')) # xMax
O.bodies.append(utils.wall(position=0,     axis=1, sense=1,  material='wallmat')) # yMin
O.bodies.append(utils.wall(position=0.018, axis=1, sense=-1, material='wallmat')) # yMax

# ── particle bed ──────────────────────────────────────────────────
# Staggered hexagonal pack in the xz-plane, repeated in 3 y-layers
# (y=0.003, 0.009, 0.015 m) for dense 3D interlocking. The outer
# spherical surfaces are flush with yMin/yMax walls — the frictionless
# walls (frictionAngle=0 for wallmat) prevent escape without bouncing.
# Odd-indexed z-rows are offset by r in x so every sphere touches 6
# neighbours (up to 4 in a square grid).  This gives a mechanically
# dense pack that self-supports under gravity and produces a smooth
# Us field (per AGENTS.md recommendation: "dense interlocking spheres
# so the bed self-supports under gravity and Us varies smoothly").
#
# Parameters
#   r = 0.003 m  →  6 spheres across 0.04 m (row 0), 6 across (row 1)
#   dz = r·√3 ≈ 0.005196 m  →  15 layers fill z=0.003→0.076 (leaves 0.076→0.096 empty for rank1)
#   y-positions: 0.003, 0.009, 0.015 m  (6 mm spacing, 0 mm wall clearance)
#   Total ≈ 3 × (8×6 + 7×6) = 270 spheres
import math

radius           = 0.003
CHAR_CORE_RADIUS = 0.00075   # 25 % of initial (visible char core)
y_positions      = [0.003, 0.009, 0.015]  # three y-layers, 0 mm wall clearance
lx, lz           = 0.04, 0.096

step    = 2.0 * radius                       # 0.006
dz      = radius * math.sqrt(3.0)            # 0.005196 — HCP vertical spacing

# Integer counts (avoids floating-point accumulation)
# First centre:  x_even = r           x_odd = 2r
# Constraint:    centre + r ≤ lx  →  last = x0 + (nx-1)·step + r ≤ lx
nx_even = int((lx - 2*radius) / step) + 1      # 6 — row 0,2,4,…
nx_odd  = int((lx - 3*radius) / step) + 1      # 6 — row 1,3,5,…  (offset by r)
nz      = int((0.080 - 2*radius) / dz) + 1   # 15 layers — leaves rank1 cells (z≥0.088) empty

sp = pack.SpherePack()
z_start = radius

# Prefer the settled bed from the standalone pre-compaction run (compact.py)
# when it is present: this starts the coupled run at rest, avoiding the
# free-fall transient of the fresh grid pack.  compact.py uses the IDENTICAL
# nested loop below, so the file's line order matches this script's sphere
# creation order one-to-one — body IDs and the sphereIDs list are identical
# either way, so no remapping is needed.
compact_file = "compact_positions.txt"
use_compacted = os.path.exists(compact_file)
if use_compacted:
    with open(compact_file) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            x, y, z = (float(v) for v in line.split())
            sp.add((x, y, z), radius)
else:
    for y_pos in y_positions:
        for k in range(nz):
            z   = z_start + k * dz
            x0  = radius + (k % 2) * radius          # stagger every other row
            nx  = nx_even if (k % 2) == 0 else nx_odd
            for i in range(nx):
                sp.add((x0 + i * step, y_pos, z), radius)

# Expected total: 3 y-layers × (8 even rows × 6 + 7 odd rows × 6) = 270
sp.toSimulation(material='spheremat')

sphereIDs = [b.id for b in O.bodies if isinstance(b.shape, Sphere)]
nlayers_y = len(y_positions)
if use_compacted:
    print(f"[YADE] Created {len(sphereIDs)} spheres from compacted positions")
else:
    print(f"[YADE] Created {len(sphereIDs)} spheres in {nz} z-layers × {nlayers_y} y-layers, "
          f"z range [{z_start:.4f}, {z_start + (nz-1)*dz:.4f}], "
          f"y positions: {y_positions}, "
          f"rows: {nx_even}/{nx_odd} (expect 270)")

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
# lambdaDot = 1.0 − Ychar (wood-remaining fraction, [0, 1]):
#   lambdaDot = 1.00  →  Ychar = 0.00  →  r = r₀ = 0.003 m
#   lambdaDot = 0.00  →  Ychar = 1.00  →  r = CHAR_CORE_RADIUS = 0.00075 m
#   lambdaDot = 0.50  →  Ychar = 0.50  →  r = 0.0025 m (halfway)
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
# Ychar-driven lambdaDot only to the worker that owns each body — the master's
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
        # lambdaDot = 1 − Ychar ∈ [0, 1]
        # Linearly interpolate radius between r₀ (all wood) and r_core (all char)
        new_rad = CHAR_CORE_RADIUS + (radius - CHAR_CORE_RADIUS) * ld
        b.shape.radius = max(new_rad, CHAR_CORE_RADIUS)

# ── VTK export ────────────────────────────────────────────────────
file_names_proc = []

def write_VTK_spheres():
    # Rank 0 holds correct lambdaDot for ALL spheres — all 324 masters and
    # synced copies (from the _sync_lambdaDot_master_slave broadcast).  Rank 1
    # keeps stale copies of rank-0 bodies that show CHAR_CORE_RADIUS artefacts.
    # Write from rank 0 only.
    if getattr(mp, 'rank', 0) != 0:
        return
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

    ld = vtk.vtkFloatArray()
    ld.SetName("lambdaDot")
    for b in sphr:
        ld.InsertNextValue(float(b.state.lambdaDot))
    polydata.GetPointData().AddArray(ld)

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
    NewtonIntegrator(gravity=(0, 0, -9.81), damping=0.7, label="newton"),
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
