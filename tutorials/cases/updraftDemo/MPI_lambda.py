import os
from yade import mpy as mp
from yade import pack, utils

counter = [1]   # mutable so we can reset after settling

# ── coupling / run control ──────────────────────────────────────────
parallelYade = True
numProcOF    = 2
nsteps       = int(2e7)   # ~20s at YADE dt~1e-6 (was 2e6, exhausted at Time=2.064)

SAVE_VTK_VIRT_PERIOD  = 0.001

# ── materials ─────────────────────────────────────────────────────
O.materials.append(FrictMat(
    young=25e6, poisson=0.5, frictionAngle=0.2618,
    density=650, label='spheremat'))
O.materials.append(FrictMat(
    young=25e8, poisson=0.5, frictionAngle=0,
    density=0, label='wallmat'))

# ── walls:  (ymport.blockMeshDict cannot parse \$variable syntax
#    and DOMAIN_DECOMPOSITION chokes on tiny particle counts.)
#    Use simple walls instead — floor at z=0 keeps the bed stacked.
#    Side walls are less critical for a settling column; we rely on
#    friction / no horizontal velocity.
# ──────────────────────────────────────────────────────────────────
O.bodies.append(utils.wall(position=0, axis=2, sense=1,  material='wallmat'))   # floor (inlet)
O.bodies.append(utils.wall(position=0.5, axis=2, sense=-1, material='wallmat')) # ceiling (outlet)
O.bodies.append(utils.wall(position=0, axis=0, sense=1,  material='wallmat'))   # xMin
O.bodies.append(utils.wall(position=0.05, axis=0, sense=-1, material='wallmat'))# xMax
O.bodies.append(utils.wall(position=0, axis=1, sense=1,  material='wallmat'))   # yMin
O.bodies.append(utils.wall(position=0.05, axis=1, sense=-1, material='wallmat'))# yMax

# ── particle bed ──────────────────────────────────────────────────
# Bed region in OF coordinates: x,y in [0,0.05], z in [0.05,0.30]
# Use makeCloud (random packing) rather than regularHexa — fewer
# spheres avoids YADE's DOMAIN_DECOMPOSITION empty-subdomain bug.
numSpheres = 100
radius = 0.004
margin = 1e-3
mn = (0.005, 0.005, 0.05 + margin)
mx = (0.045, 0.045, 0.30 - margin)

sp = pack.SpherePack()
sp.makeCloud(mn, mx, rMean=radius, rRelFuzz=0.1, num=numSpheres)
O.bodies.append([sphere(c, r, material='spheremat') for c, r in sp])

# ── gravity settling (YADE only, no OF coupling yet) ──
# Spheres are created at random heights (z=0.05→0.30); let them fall
# and pack at the floor before starting the coupled simulation.
# GlobalStiffnessTimeStepper reduces dt to ~1e-7 for stiff contacts,
# so 5M steps ≈ 0.5s virtual time — enough for all spheres to reach
# the floor (t_fall = sqrt(2·0.3/9.81) = 0.25s from top of bed).
print(f"[YADE] Starting gravity settling — {len(O.bodies)} bodies (incl. walls)")
print(f"[YADE] O.time = {O.time:.6f}")
O.run(5000000, True)
zMin = min(b.state.pos[2] for b in O.bodies if isinstance(b.shape, Sphere))
zMax = max(b.state.pos[2] for b in O.bodies if isinstance(b.shape, Sphere))
print(f"[YADE] Settled: O.time = {O.time:.4f}s, z range [{zMin:.4f}, {zMax:.4f}]")

counter[0] = 1  # reset VTK counter for coupled phase

sphereIDs = [b.id for b in O.bodies if isinstance(b.shape, Sphere)]
print(f"[YADE] Created {len(sphereIDs)} spheres")

os.makedirs("spheres", exist_ok=True)

# ── foam coupling ─────────────────────────────────────────────────
fluidCoupling = FoamCoupling()
fluidCoupling.couplingModeParallel = parallelYade
fluidCoupling.isGaussianInterp = False

fluidCoupling.SetOpenFoamSolver("porousGasificationFoam", numProcOF)
fluidCoupling.setIdList(sphereIDs)

# ── particle shrinkage ────────────────────────────────────────────
# Particles shrink toward a char core as volatiles (gas + tar) leave;
# once CHAR_CORE_RADIUS is reached, shrinkage stops and the particle is
# NOT erased.  Erasing a body mid-run is unsafe: FoamCoupling::setHydroForce()
# runs every iteration against a bIds list that is only rebuilt on coupling
# exchanges, and dereferences each id with no null check
# (FoamCoupling.cpp:655, `Body::byId(...)->state`).  A body erased between
# exchanges leaves a dangling id there → null deref → SIGSEGV on rank 1.
# Clamping instead of erasing keeps every body alive, so the id list never
# goes stale.  It also neutralises the lambdaDot default of 0.0 (core/State.hpp):
# a not-yet-coupled particle clamps to the floor radius rather than collapsing
# to zero and being destroyed.
CHAR_CORE_RADIUS = 0.0027

def changeRadius():
    for b in O.bodies:
        if not isinstance(b.shape, Sphere):
            continue
        new_rad = b.shape.radius * b.state.lambdaDot
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
    PyRunner(command="changeRadius()",
             virtPeriod=0.1 * SAVE_VTK_VIRT_PERIOD,
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
