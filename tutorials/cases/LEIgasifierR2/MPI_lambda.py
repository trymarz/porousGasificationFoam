"""Yade DEM + porousGasificationFoam coupling script for LEIgasifierR2.

Copied from LEIgasifierDEM (issue #104); both coupling channels are live,
though with chemistry off lambdaDot is zero and r = 1 throughout --
  - lambdaDot (OpenFOAM -> Yade): per-FVM-cell apparent-volume factor r
    (lambdaMode apparentVolume, constant/lambdaDict). FoamCoupling multiplies
    each body's state.lambda_ by r once per exchange; this script only reads
    lambda_ back to set the sphere's radius (radius = lambda_ * initial
    radius, see changeRadius() below).
  - Us (Yade -> OpenFOAM): FoamCoupling maps sphere velocities onto solid
    cells every step (isGaussianInterp), same mechanism as the reverse
    lambdaDot mapping. No Python code needed for this side.

The spheres start from relaxedSpheres.txt, written by relax.py: one
hexagonal layer filling the bed up to z_fill, settled under gravity before
coupling, so the run starts at rest. R2 adds feed and discharge: spheres are
inserted in the freeboard at a fixed rate and removed at the grate at the same
rate, and FoamCoupling's id list follows (insertBodyId / eraseId). The solid
mass balance goes to balance.txt (README.md, "Feed and discharge"). Spheres in cells without FVM solid get
r = 1 (lambdaDict minSolidVolume), keep their size and carry the bed.

Insertion is not distributed: the run needs exactly one Yade worker.

Usage (2 Yade ranks pre-launched, 2 OpenFOAM ranks spawned by FoamCoupling):
    mpirun -n 2 yade MPI_lambda.py
"""
import os
import random
import sys

sys.path.insert(0, os.getcwd())

from yade import mpy as mp
from yade import export, pack
from yade.system import O

import demGeometry as geom
import leiGeometry as lei

comm = mp.comm_slave   # communicator for worker ranks

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
parallelYade       = True
numProcOF          = 2          # OpenFOAM MPI ranks

writeInterval      = 0.25       # VTK write interval (virtual seconds)
radiusInterval     = 0.01       # changeRadius() period (virtual seconds)
balanceInterval    = 0.01       # balance.txt period (virtual seconds)
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

# Feed reservoir: spheres parked far from the mesh, all DOFs blocked, created
# before the scene is split; feeding releases them (README.md).
nFeed = int(NSTEPS * yadeDtFixed * geom.feed_rate) + 2
parked = []
for k in range(nFeed):
    b = sphere(geom.park_position(k), sphere_radius, material="spheremat")
    b.state.blockedDOFs = "xyzXYZ"
    parked.append(O.bodies.append(b))
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
    for i in list(liveIDs):
        if i >= len(O.bodies) or O.bodies[i] is None:
            continue
        b = O.bodies[i]
        new_rad = sphere_radius * b.state.lambda_
        if new_rad < 1e-4:
            removeSphere(b, burnt=True)
        else:
            b.shape.radius = new_rad

# ---------------------------------------------------------------------------
# Feed and discharge, with the coupling's id list kept in step
# ---------------------------------------------------------------------------
liveIDs = set(sphereIDs)
gone    = []          # discharged spheres, parked like the reservoir
rng     = random.Random(geom.feed_seed)
bal     = dict(n_in=0, n_out=0, n_burnt=0, m_in=0.0, m_out=0.0, m_burnt=0.0,
               m0=None, blocked=0)


def abort(msg):
    mp.mprint("[FEED] FATAL: " + msg, force=True)
    mp.comm.Abort(1)


def removeSphere(b, burnt=False):
    m = geom.sphere_mass(b.shape.radius)
    key = "burnt" if burnt else "out"
    bal["n_" + key] += 1
    bal["m_" + key] += m
    liveIDs.discard(b.id)
    if not fluidCoupling.eraseId(b.id):
        abort("sphere %d was not in the coupling" % b.id)
    # Parked, not erased: mp.bodyErase() leaves FoamCoupling a null body.
    k = nFeed + bal["n_out"] + bal["n_burnt"]
    b.state.blockedDOFs = "xyzXYZ"
    b.state.vel = b.state.angVel = Vector3.Zero
    b.state.pos = Vector3(*geom.park_position(k))
    gone.append(b.id)


def freeSpot(spheres):
    """A random feed position with no overlap, or None."""
    lo, hi = geom.feed_band
    near = [(b.state.pos, b.shape.radius) for b in spheres
            if b.state.pos[2] > lo - 2.0 * sphere_radius]
    for _ in range(50):
        x = rng.uniform(-geom.feed_halfwidth, geom.feed_halfwidth)
        z = rng.uniform(lo, hi)
        if all((p[0] - x) ** 2 + (p[2] - z) ** 2 >= (r + sphere_radius) ** 2
               for p, r in near):
            return x, z
    return None


def insertSphere(x, z):
    if not parked:
        abort("feed reservoir empty at t = %.4f s" % O.time)
    bid = parked.pop(0)
    b = O.bodies[bid]
    b.state.pos = Vector3(x, 0.0, z)
    b.state.vel = b.state.angVel = Vector3.Zero
    b.state.blockedDOFs = geom.blocked_dofs
    fluidCoupling.insertBodyId(bid)
    liveIDs.add(bid)
    bal["n_in"] += 1
    bal["m_in"] += geom.sphere_mass()


def liveSpheres():
    return [O.bodies[i] for i in liveIDs]


def feedAndDischarge():
    # Only on the step before an exchange, which rebuilds FoamCoupling's ids:
    # erasing earlier leaves stale ids for setHydroForce() (null body).
    if (O.iter + 1) % max(1, fluidCoupling.dataExchangeInterval) != 0:
        return
    try:
        _feedAndDischarge()
    except Exception as e:   # a PyRunner error would hang the coupled ranks
        abort("feedAndDischarge: %r" % e)


def _feedAndDischarge():
    # Continuous equivalent of the screws: both rates are fixed (README.md).
    if mp.rank == 0:
        return
    if mp.numThreads != 2:
        abort("feed and discharge need exactly one Yade worker")
    due = int(O.time * geom.feed_rate)
    while bal["n_in"] < due:
        spot = freeSpot(liveSpheres())
        if spot is None:
            bal["blocked"] += 1
            abort("feed blocked at t = %.4f s: the bed reached the feed band"
                  % O.time)
        insertSphere(*spot)
    grate = sorted((b for b in liveSpheres()
                    if b.state.pos[2] - lei.z_grate < geom.discharge_band),
                   key=lambda b: b.state.pos[2])
    for b in grate[:max(0, due - bal["n_out"])]:
        removeSphere(b)


def bedLevel(spheres, vmax=0.1):
    """Mean top-layer height: highest resting sphere top per one-diameter x column."""
    zmax = geom.feed_band[0] - 2.0 * sphere_radius
    top = {}
    for b in spheres:
        if b.state.vel.norm() < vmax and b.state.pos[2] < zmax:
            k = int((b.state.pos[0] + 10.0) // (2.0 * sphere_radius))
            top[k] = max(top.get(k, -1e9), b.state.pos[2] + b.shape.radius)
    return sum(top.values()) / len(top) if top else float("nan")


def writeBalance():
    # Solid mass in = out + inventory change, written every balanceInterval.
    if mp.rank == 0:
        return
    sph = liveSpheres()
    inv = sum(geom.sphere_mass(b.shape.radius) for b in sph)
    if bal["m0"] is None:
        bal["m0"] = inv
    resid = bal["m_in"] - bal["m_out"] - bal["m_burnt"] - (inv - bal["m0"])
    nCoupled = fluidCoupling.getNumBodies()
    nBodies = sum(1 for b in O.bodies if isinstance(b.shape, Sphere)) \
        - len(parked) - len(gone)
    new = not os.path.exists("balance.txt")
    with open("balance.txt", "a") as f:
        if new:
            f.write("# t nSpheres nCoupled nIn nOut nBurnt mIn mOut mBurnt "
                    "inventory residual bedLevel\n")
        f.write("%.4f %d %d %d %d %d %.8e %.8e %.8e %.8e %.3e %.5f\n" % (
            O.time, nBodies, nCoupled, bal["n_in"], bal["n_out"],
            bal["n_burnt"], bal["m_in"], bal["m_out"], bal["m_burnt"], inv,
            resid, bedLevel(sph)))
    if nCoupled != len(sph) or nBodies != len(sph):
        abort("coupling ids %d, spheres %d, tracked %d"
              % (nCoupled, nBodies, len(sph)))

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
        if isinstance(b.shape, Sphere) and b.id in liveIDs:
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
    PyRunner(command="feedAndDischarge()",    iterPeriod=1),
    PyRunner(command="writeBalance()",        virtPeriod=balanceInterval,
             firstIterRun=1),
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
mp.mprint("LEIgasifierR2 run finished")

exit()
