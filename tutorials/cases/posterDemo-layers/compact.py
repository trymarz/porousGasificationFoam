# compact.py — Gravity-only DEM pre-compaction for posterDemo-layers.
#
# Run ONCE (single-process, non-MPI) before the coupled CFD-DEM run:
#
#     yade compact.py
#
# Why: when the coupled solver starts, YADE and OpenFOAM couple from
# iteration zero.  The grid-based packing in MPI_lambda.py leaves small
# gaps, so the spheres free-fall under gravity while chemistry and
# hydrodynamics are already active — an unphysical startup transient
# (freefall through the fluid, spurious drag, continuity-error spikes).
#
# This script builds the IDENTICAL sphere packing, lets the bed settle
# under gravity alone (no FoamCoupling, no MPI), and writes the settled
# centres to compact_positions.txt.  MPI_lambda.py then loads those
# positions instead of the fresh grid so the coupled run starts at rest.
#
# Why standalone and not just O.run() inside MPI_lambda.py: YADE's MPI
# engines (GlobalStiffnessTimeStepper, InsertionSortCollider) sync across
# ranks and block before mp.mpirun() sets up the coupling framework, so
# O.run() cannot settle the bed in MPI mode (see updraftDemo/MPI_lambda.py).
#
# Determinism: the materials, walls, and the EXACT nested packing loop
# below are copied verbatim from MPI_lambda.py, so spheres are created in
# the same order.  The i-th line of compact_positions.txt therefore maps
# to the i-th sphere MPI_lambda.py creates — no body-ID remapping needed.

import math

# ── materials (must match MPI_lambda.py) ──────────────────────────
O.materials.append(FrictMat(
    young=25e6, poisson=0.5, frictionAngle=0.2618,
    density=650, label='spheremat'))
O.materials.append(FrictMat(
    young=25e8, poisson=0.5, frictionAngle=0,
    density=0, label='wallmat'))

# ── walls (must match MPI_lambda.py: 0.04 x 0.018 x 0.24 m domain) ──
O.bodies.append(utils.wall(position=0,     axis=2, sense=1,  material='wallmat')) # floor (inlet)
O.bodies.append(utils.wall(position=0.24,  axis=2, sense=-1, material='wallmat')) # ceiling (outlet)
O.bodies.append(utils.wall(position=0,     axis=0, sense=1,  material='wallmat')) # xMin
O.bodies.append(utils.wall(position=0.04,  axis=0, sense=-1, material='wallmat')) # xMax
O.bodies.append(utils.wall(position=0,     axis=1, sense=1,  material='wallmat')) # yMin
O.bodies.append(utils.wall(position=0.018, axis=1, sense=-1, material='wallmat')) # yMax

# ── particle bed (EXACT same parameters & loop order as MPI_lambda.py) ──
radius      = 0.003
y_positions = [0.004, 0.01]  # two y-layers, 1 mm wall clearance
lx, lz      = 0.04, 0.096

step    = 2.0 * radius                       # 0.006
dz      = radius * math.sqrt(3.0)            # 0.005196 — HCP vertical spacing

nx_even = int((lx - 2*radius) / step) + 1      # 6 — row 0,2,4,…
nx_odd  = int((lx - 3*radius) / step) + 1      # 6 — row 1,3,5,…  (offset by r)
nz      = int((lz - 2*radius) / dz) + 1        # 18 layers

sp = pack.SpherePack()
z_start = radius
for y_pos in y_positions:
    for k in range(nz):
        z   = z_start + k * dz
        x0  = radius + (k % 2) * radius          # stagger every other row
        nx  = nx_even if (k % 2) == 0 else nx_odd
        for i in range(nx):
            sp.add((x0 + i * step, y_pos, z), radius)

sp.toSimulation(material='spheremat')

sphereIDs = [b.id for b in O.bodies if isinstance(b.shape, Sphere)]
print(f"[compact] Created {len(sphereIDs)} spheres (expect 216), "
      f"settling under gravity…")

# ── DEM engines (gravity only — no FoamCoupling, no PyRunner, no MPI) ──
# damping=0.7 (near-critical) dissipates the settling kinetic energy quickly
# so the bed reaches a static configuration within the 1 s window. It also
# suppresses elastic rebound at contacts, matching MPI_lambda.py so the
# compacted bed stays put during coupling instead of drifting upward.
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
        label="ts",
    ),
    NewtonIntegrator(gravity=(0, 0, -9.81), damping=0.7, label="newton"),
]

# ── settle ────────────────────────────────────────────────────────
# Run 1.0 s of virtual time for thorough settling.  Seed a sane dt and
# let GlobalStiffnessTimeStepper converge to its stable value first
# (the default pre-run O.dt is far too small to size the run off).  Then
# step in capped batches, recomputing the remaining count from the
# current adaptive dt each batch, so the total lands near 1 s without
# overshooting if the timestepper raises dt.
settleTarget = 1.0
O.dt = 1e-6
O.run(200, True)                       # let the timestepper find a stable dt
while O.time < settleTarget:
    nrem = int((settleTarget - O.time) / O.dt) + 1
    O.run(min(nrem, 200000), True)

print(f"[compact] Settled at t = {O.time:.4f} s, {O.iter} iterations")

# ── save final positions (sphere creation order = file line order) ──
with open("compact_positions.txt", "w") as f:
    for b in O.bodies:
        if isinstance(b.shape, Sphere):
            p = b.state.pos
            f.write(f"{p[0]:.10e} {p[1]:.10e} {p[2]:.10e}\n")

print(f"[compact] Saved {len(sphereIDs)} positions to compact_positions.txt")
exit()
