"""Yade DEM + porousGasificationFoam coupling script for LEIgasifierDEM.

Twin-domain case (issue #104): unlike LEIgasifierPrescribedUs, both channels
are live here --
  - lambdaDot (OpenFOAM -> Yade): per-FVM-cell continuum rate
    (exactDifferentialLambdaDot, constant/lambdaDict). NewtonIntegrator
    integrates it into each body's state.lambda_; this script only reads
    lambda_ back to set the sphere's physical radius (radius = lambda_ *
    initial radius -- the PR#43 contract, see changeRadius() below).
  - Us (Yade -> OpenFOAM): FoamCoupling maps sphere velocities onto solid
    cells every step (isGaussianInterp), same mechanism as the reverse
    lambdaDot mapping. No Python code needed for this side.

Spheres pack the full bed cross-section -- every mesh block that holds bed
per system/setFieldsDict (z=[0.20,1.00], not just the old, narrower
throat+combustion sub-box) -- and fall under gravity from t=0; there is no
separate DEM-only settling phase.

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
yadeDtFixed        = 5e-6       # reused from LEIgasifierPrescribedUs -- already
                                 # tuned for ~4 mm spheres at young=25e6 below
O.dynDt            = False
O.dt               = yadeDtFixed

timeRatio = max(1, int(round(writeInterval / yadeDtFixed)))
NSTEPS    = int(os.environ.get('YADE_NSTEPS', 2000000))

if mp.rank == 0:
    print(f"[EXPORT] yadeDt={yadeDtFixed}  writeInterval={writeInterval}  iterPeriod={timeRatio}")

sphere_radius = 0.004   # initial radius (m) -- 4 mm biomass pellets, monodisperse

# ---------------------------------------------------------------------------
# Full-domain packing zones -- one aligned box per make_mesh.py mesh block
# that holds bed, i.e. blocks 1-4 (z >= 0.20), matching this case's own
# system/setFieldsDict, which fills exactly z=[0.20,1.01] with wood/char and
# leaves block 0 (freeboard, z<0.20) at the ambient gas default (porosityF=1,
# no wood/char) -- packing spheres there would put solid where the CFD side
# has none. The outer shells 5/6 are gas-only, not part of the bed, either.
# Tapered blocks 1 and 3 use their narrower (inner, +-0.08) X extent so the
# initial cloud never starts outside the walls at either end of the taper.
# Y matches the sub-box's existing margin inside the +-0.008 slab half-depth
# (unchanged from LEIgasifierPrescribedUs -- reused as-is, not re-derived).
# ---------------------------------------------------------------------------
pack_zones = [
    (-0.08, 0.08, 0.20, 0.30),   # block 1: constriction (narrower bound)
    (-0.08, 0.08, 0.30, 0.32),   # block 2: throat
    (-0.08, 0.08, 0.32, 0.42),   # block 3: expansion (narrower bound)
    (-0.15, 0.15, 0.42, 1.00),   # block 4: main upper (drying + pyrolysis)
]
pack_y_lo, pack_y_hi = -0.006, 0.006

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
# Sphere packing — random packing across the full reactor cross-section
# ---------------------------------------------------------------------------
for x_lo, x_hi, z_lo, z_hi in pack_zones:
    sp = pack.SpherePack()
    sp.makeCloud(
        Vector3(x_lo, pack_y_lo, z_lo),
        Vector3(x_hi, pack_y_hi, z_hi),
        rMean=sphere_radius,
        rRelFuzz=0.0,
    )
    O.bodies.append([sphere(c, r, material='spheremat') for c, r in sp])

sphereIDs = [b.id for b in O.bodies if type(b.shape) == Sphere]
os.makedirs("spheres", exist_ok=True)

if mp.rank == 0:
    print(f"[DEM] packed {len(sphereIDs)} spheres across {len(pack_zones)} zones (full domain)")

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
# Radius shrinkage driven by lambda (PR#43 contract)
# ---------------------------------------------------------------------------
def changeRadius():
    # lambda_ is the value NewtonIntegrator integrates from lambdaDot
    # (state.lambda_ += lambdaDot*dt each DEM step); it is a dimensionless
    # scale around 1.0, same quantity the MicroTGA lambdaDot fixtures' spring
    # rest-length scaling reads (target_L = L0 * lambda_avg). Radius must be
    # scaled the same way -- radius = sphere_radius * lambda_ -- not
    # multiplied onto the current radius every step (that was the pre-PR#43
    # bug: lambdaDot is now a rate, not a one-step [0,1] shrink fraction, so
    # radius *= lambdaDot would erase every sphere on the first step).
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
    NewtonIntegrator(gravity=(0, 0, -9.81), damping=0.2, label="newton"),
    PyRunner(command="changeRadius()",        virtPeriod=writeInterval * 0.1),
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
mp.mprint("LEIgasifierDEM run finished")

exit()
