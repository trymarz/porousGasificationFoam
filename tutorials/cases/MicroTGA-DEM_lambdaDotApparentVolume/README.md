# MicroTGA-DEM_lambdaDotApparentVolume — reference DEM-spring case

This is the case to start from when you build your own DEM-spring case
(#207). It is also a regression fixture (`applications/test/regression/cases_yade.list`),
so leave the original as it is and work on a copy.

Facts below were checked against PGF at `8a0217d` and Foam-Yade at the pinned
submodule commit `42126e8`. If you change either, check the facts again.

## Why this fixture

There are six `MicroTGA-DEM_lambdaDot*` fixtures. They differ only in
`constant/lambdaDict`, plus the `Allrun` assertion that goes with it.
`...ApparentVolume` is the template because:

- `lambdaMode apparentVolume` gets λ from the solid's apparent volume
  (`rhoApparent` of each specie), and every value it uses has a stated
  source in `thermoData/solidThermoData`. The `exactDifferential`
  fixtures use `dlambdaOverDTs`/`dlambdaOverDYmi` values chosen so the
  regression signal is visible, not for physical accuracy (see
  `doc/case-setup.md`).
- With these values λ only ever decreases (shrinkage), and that is the
  direction a char-gasification variant needs.
- It is the only fixture with its own `constant/solidThermophysicalProperties`
  (Corymbia wood and char), so a new case already has a place to put its
  materials.

## What the case is

- **Mesh**: a closed 40 mm cube with 20³ cells (2 mm each) and three wall
  patches. The `inlet`/`outlet` entries in `0.orig/` match no patch.
- **Bed**: `solidBed`, the cells inside a 10.2 × 10.2 × 20.2 mm box
  (`topoSetDict`). `setFieldsDict` sets `porosityF 0.85` there; everywhere
  else it is 1. The solid starts as `Ywood 1` at `Ts 650 K` in N₂ gas at
  `T 850 K`.
- **Chemistry**: the shared pyrolysis scheme in `constant/chemistryProperties`:
  wood → targas, wood → char1, char1 → 0.65 char + 0.35 gas.
- **Run**: `endTime 0.002`, `deltaT 1e-6`, so 2000 solver steps, with a
  fixed time step (`adjustTimeStep no`).
- **DEM**: 100 spheres with springs between neighbours, in the same
  10 × 10 × 20 mm box. Yade runs at `O.dt = 1e-6`, so there is one coupling
  exchange per solver step.
- **What it checks**: `Allrun` checks that `lambdaDot` (here the factor r)
  is > 0 and that the mean λ has dropped below 1. In the baseline, λ
  averages 1 − 8.5e-6 at t = 0.002. `system/regressionInvariants` checks
  that the spring forces reach `NewtonIntegrator` (#231).

## Running it

You need a solver built with `pgf build --yade` against a matching
Foam-Yade build. The particle buffer layout is a compile-time constant, so
rebuild both together. `Allrun` launches `mpirun -n 2 yade MPI_lambda.py`.
Yade then spawns the OpenFOAM ranks itself, so the run uses **4 MPI
processes** (2 Yade + 2 OpenFOAM).

## Copying it

Most files here are symlinks into `../MicroTGA-DEM_lambdaDot_common/`.
`MPI_lambda.py` also imports `../DEM_common/yadeEndpoint.py`, and `Allrun`
calls `../../../applications/test/regression/tools/checkFieldSign.py`. So:

```sh
cp -rL MicroTGA-DEM_lambdaDotApparentVolume myCase   # -L: copy files, not links
```

Keep `myCase` next to `DEM_common/`, or copy `DEM_common/` along with it and
fix the `sys.path` line in `MPI_lambda.py`. The `checkFieldSign.py` step and
`reference/` only matter to the regression run, so you can delete them from
your copy.

## `MPI_lambda.py`, parameter by parameter

Line numbers refer to `MPI_lambda.py`.

### Time step and output

| Name | Value | Meaning |
|---|---|---|
| `yadeDtFixed`, `O.dt` | `1e-6` s | DEM time step. Must match the coupling rules (see *Changing the time step*). |
| `O.dynDt` | `False` | Fixed `O.dt`. `yadeEndpoint.yadeSteps` refuses to work with `dynDt`. |
| `writeInterval` | `0.1` s | Spring VTK and `dtInfo.txt` cadence, as `timeRatio = writeInterval/O.dt` steps. This is longer than the 2000-step run, so both write only at iteration 1 (`firstIterRun=1`). |
| `vtkSphereIterPeriod` | `NSTEPS // 10` | Cadence for sphere VTK, `logLambdaUs()` and `springForceAtNewton`. |
| `NSTEPS` | from `controlDict` | `yadeEndpoint.yadeSteps(O.dt)` = solver steps × `k`. See *Changing the time step*. |

### Ranks and coupling

| Name | Value | Meaning |
|---|---|---|
| `numProcOF` | `2` | Used twice: `SetOpenFoamSolver("porousGasificationFoam", numProcOF)` spawns this many OpenFOAM ranks with `-parallel` (`MPI_Comm_spawn` in `FoamCoupling::StartFoamSolver`), and `mp.mpirun(NSTEPS, np=numProcOF)` sets the number of Yade processes. |
| `parallelYade` / `fluidCoupling.couplingModeParallel` | `True` | No effect: `couplingModeParallel` is commented out in Foam-Yade's `FoamCoupling.hpp`. Whether Yade runs in parallel is decided by its process count (`serialYade` when it is 1). |
| `setIdList(sphereIDs)`, `setNumParticles` | all spheres | Coupled bodies. The two box walls are not coupled. |
| `O.periodic` | `False` | No periodic cell. |

### Material and packing

| Name | Value | Meaning |
|---|---|---|
| `numspheres` | `100` | Number of spheres `makeCloud` places. |
| `rMean`, `rRelFuzz` | `0.00075` m, `0` | Every sphere has radius 0.75 mm. |
| `seed` | `1` | Fixed packing. Without it every run and every rank packs differently, and baselines do not reproduce (#139). |
| `maxvalX/Y/Z` | 10, 10, 20 mm | Box the spheres fill, centred on the origin. It matches `solidBed`. |
| `margin` | `5e-5` m | Gap between the cloud and the box faces. |
| `young` | `25e6` Pa | `spheremat`. Contact normal stiffness is `kn = E·R` = 18750 N/m for two spheres (`Ip2_FrictMat_FrictMat_FrictPhys`). |
| `poisson` | `0.5` | In Yade's `FrictMat` this is **ks/kn**, the shear-to-normal stiffness ratio, not Poisson's ratio. |
| `frictionAngle` | 15° | Contact friction between spheres. The interaction uses the smaller of the two materials' angles, so contacts with the walls (angle 0) are frictionless. |
| `density` | `1200` kg/m³ | DEM sphere density. Each sphere's mass is 4/3·π·R³·ρ = 2.12e-6 kg. It does not change when λ does. |
| `wallmat` | `young·100`, density 0 | The two fixed boxes. |
| walls | two `box`es at y = ±5 mm | Only the y faces are closed. Spheres can leave through x and z. |

### Springs

| Name | Value | Meaning |
|---|---|---|
| `n_neighbors` | `6` | Each sphere is linked to its 6 nearest neighbours (KD-tree). Duplicate pairs are dropped. |
| `L0` | per spring | Initial centre distance, written to `springs/springsList.txt`. |
| `mass` | `0.000268` kg | Labelled "same as wood sample". It is **not** the sphere mass (2.12e-6 kg). It is used only to compute `spring_k`. |
| `spring_damping` | `1.0` | Used only in `spring_k = spring_damping²/(4·mass)` = 933 N/m, the critical-damping relation c² = 4km solved for k. The script applies no spring dashpot. |
| `min_elongation_percent` | `80` | **Freeze**: when `L ≤ 0.8·L0` the spring pushes with no force that step, and contacts carry the load. Measured against `L0`, not the λ-scaled rest length. |
| `max_elongation_percent` | `100` | **Break**: when `L > 2·L0` the spring is removed for good. |
| λ clamp | `[0.1, 2.0]` | The mean of the two endpoint λ values is clamped before `target_L = L0·λ`. The clamp affects only the force, not the stored λ. |

The force is Hooke's law towards `target_L`:
`F = spring_k·(L − L0·λ̄)` along the line between centres.
Because the freeze threshold is `0.8·L0`, a mean λ below 0.8 never
reaches its rest length: the spring stops acting first. If your case shrinks
that far, scale the freeze threshold by λ.

### Damping and gravity

| Name | Value | Meaning |
|---|---|---|
| `NewtonIntegrator(damping=0.99)` | | Yade's non-viscous (Cundall) damping. 0.99 is close to quasi-static, and this is the only damping on the spheres apart from fluid drag. |
| `gravity` | `(0, 0, 0)` | Off. |

### Contact detection and `mpy` flags

| Name | Value | Meaning |
|---|---|---|
| `collider.verletDist` | `0.00075` m | Bounding-box enlargement, so the collider does not re-sort every step. |
| `mp.DOMAIN_DECOMPOSITION` | `True` | `mpy` splits the bodies among the workers. Rank 0 is the master and holds none, so with 2 Yade processes one worker holds all 100 spheres. |
| `mp.FLUID_COUPLING`, `mp.fluidBodies` | `True`, spheres | Tells `mpy` that a `FoamCoupling` engine is present and which bodies it couples. |
| `mp.ERASE_REMOTE_MASTER`, `mp.USE_CPP_INTERS`, `mp.VERBOSE_OUTPUT`, `mp.YADE_TIMING` | | These repeat `mpy`'s defaults. |
| `mp.REALLOC_FREQUENCY` | `12` | No effect: `mpy`'s flag is called `REALLOCATE_FREQUENCY` (default 0, never reallocate). |

### Fluid-side settings (`constant/yadeProperties`)

`nu 1e-6` and `fluidDensity 1.0` go into the Stokes drag that OpenFOAM
returns for each sphere,
`F = 3π·d·ν·ρ_f·(U_gas − v)`, plus a torque (`FoamYade::stokesDragForce`,
non-Gaussian path). These are fixed values, not the gas thermo, and the
reaction on the gas (`uSource`) is not used by PGF's momentum equation.
`partDensity` is passed in, but it is read only by `addedMassForce`, which
this path never calls.

## Why the engines run in this order

Each step Yade runs `O.engines` from top to bottom:

1. **`ForceResetter`** zeroes the force accumulators. Everything added by
   `addF`/`addForce` lasts one step only, so every force must be added after
   this engine.
2. **`InsertionSortCollider`** finds candidate contacts from the bounding
   boxes.
3. **`InteractionLoop`** computes contact geometry, stiffness and the
   Cundall–Strack contact forces.
4. **`FoamCoupling`**. On exchange steps (`iter % k == 0`) it sends position,
   velocity, angular velocity, radius and λ to OpenFOAM. It receives drag,
   torque and slot 6, and in `apparentVolume` mode it applies
   `λ ← λ·r` (`applyLambdaFactor`). It then exchanges the time step. On
   every step, `setHydroForce` adds the last drag and torque received and
   sets `lambdaDot` (0 in this mode). It has to come after `ForceResetter`,
   or the drag would be erased.
5. **`apply_spring_forces()`** reads the λ that step 4 has just updated and
   adds the spring forces. It has to come before `NewtonIntegrator`: run
   after it, the forces are reset unused (#231, which
   `regressionInvariants` checks).
6. **`spring_force_at_newton()`** logs the spring force added this step.
   It has to come after step 5 in the same iteration.
7. **`NewtonIntegrator`** integrates velocities and positions with damping,
   and λ += lambdaDot·dt. In `apparentVolume` mode lambdaDot is 0, so λ
   changes only through r in step 4.
8. **Output**: spring VTK, sphere `VTKRecorder`, `logLambdaUs()` (to
   `lambdaUsHistory.txt`) and `dtInfo.txt`. They run after `NewtonIntegrator`
   so they record the end-of-step state.

On the OpenFOAM side, each solver step runs before `pyrolysisZone.evolve()`:
`lambdaDotModel::updateLambdaDot()`, then the exchange
(`FoamYade::setParticleAction`), then `updateParticleFields()`. That last
call averages the particle velocities and λ into each cell and smooths them
into `Us` and `lambda`. The chemistry computes r later in the step, so the r
sent at step n is the one from step n−1.

## Changing the particle count

1. Set `numspheres`. With `rRelFuzz 0` and this box, 100 spheres fill
   about 9 % of the volume. `makeCloud` places fewer than asked when they
   do not fit, so check `len(sp)` after the call.
2. Fit the sphere size to the mesh. `updateParticleFields` averages per
   cell, and with 2 mm cells and 1.5 mm spheres many bed cells hold no
   sphere at all. Refine the mesh or change `rMean` together.
3. Keep `rMean`, the bed box in `MPI_lambda.py` (`maxvalX/Y/Z`) and the
   `solidBed` box (`topoSetDict`, `setFieldsDict`) consistent.
4. More spheres means more springs: between 3 and 6 per sphere with
   `n_neighbors 6`, because shared pairs are counted once. The script prints
   the total (`[SPRINGS] Created …`).

## Changing the rank count

There are two separate counts:

- **OpenFOAM ranks** = `numProcOF` = `numberOfSubdomains` in
  `system/decomposeParDict`. The product of the `simple` `n` entries must
  equal that number. Change all three together, then run `decomposePar`.
- **Yade processes** = `mpirun -n` in `Allrun` = `np` in `mp.mpirun`.
  The script uses `numProcOF` for both. If you want them to differ, give
  `mp.mpirun` its own variable. Keep `np` equal to `mpirun -n`; if it is
  larger, `mpy` spawns the difference (`mpy.initialize`). One Yade process is the
  master and holds no spheres, so `n` Yade processes give `n − 1` DEM
  subdomains. A single Yade process runs serial Yade.

Total MPI processes = Yade processes + OpenFOAM ranks.

## Changing the time step

- `deltaT` in `controlDict` sets the OpenFOAM step and `O.dt` sets the DEM
  step. Yade exchanges every `k` DEM steps:
  - in `apparentVolume` mode, `k = int(deltaT/O.dt)`, or 1 when
    `O.dt ≥ deltaT`;
  - in the rate modes (`constant`, `exactDifferential`), Foam-Yade aborts
    unless `deltaT/O.dt` is an integer (`setExchangeInterval`, #230).

  Keep `deltaT/O.dt` an integer in every mode, otherwise Yade's time drifts
  from the solver's.
- `NSTEPS` follows from `controlDict` through `yadeEndpoint.yadeSteps`, which
  needs `adjustTimeStep no`, `stopAt endTime`, `startFrom startTime` and a
  fixed `O.dt`. Never set `NSTEPS` by hand: a mismatch leaves one side
  waiting forever.
- DEM stability: `O.dt` must stay below the contact time scale. The P-wave
  estimate R·√(ρ/E) is 5.2e-6 s here, so `O.dt = 1e-6` is about 0.2 of it.
  If you change `young`, `density` or `rMean`, check it again
  (`PWaveTimeStep()` in Yade prints it). The springs (k = 933 N/m) are much
  softer than the contacts (18750 N/m) and do not limit the time step.
- Run length: change `endTime`. The run takes `endTime/deltaT` exchanges.

## What λ drives today, and what it does not (#206)

λ is a per-particle scalar, `State::lambda_` in Yade, starting at 1.

**It drives:**
- the spring rest length `L0·λ̄` in `apply_spring_forces()` (clamped to
  [0.1, 2]). This is the **only** place λ has a mechanical effect;
- PGF's `lambda` field: Yade sends λ back to OpenFOAM (buffer slot 10), and
  `lambdaDotModel` averages and smooths it onto the mesh. It is output only;
  no solver equation uses it.

**It does not drive:**
- the sphere radius. `Sphere::radius` stays at `rMean`, so contact overlap
  and stiffness (`kn = E·R`) do not change;
- mass or inertia;
- the radius OpenFOAM receives (slot 9). That is the original radius, so
  the Stokes drag diameter does not change either;
- porosity. PGF's `porosityF` follows the chemistry's solid volume loss on
  its own, whatever λ does.

So a shrinking particle in this case pulls on its springs, but keeps its
contact size and mass. Making the radius and mass follow λ is #206, which is
parked.

**How r is computed** (`apparentVolumeLambdaDot.H`). In each cell,
`V_old = Σ Ym_i/rhoApparent_i` and
`dV_chem = deltaT·Σ sRhoS_i/rhoApparent_i` (chemistry only, not transport),
and `r = cbrt(1 + dV_chem/V_old)`. Every particle in a cell gets the cell's
r. Cells with `porosityF` at or above the connected threshold
(`connectedAboveSolidFraction` in `pyrolysisProperties`) get r = 1. Here,
wood → char1 keeps the volume (both 583 kg/m³), while wood → targas and
char1 → 0.65 char (459 kg/m³) both shrink it, so r ≤ 1.

## Files that differ from the other fixtures

Only these files are this case's own: `constant/lambdaDict`,
`constant/solidThermophysicalProperties`, `Allrun`, `0.orig/`,
`system/regressionInvariants`, `reference/` and this README. Everything else
is shared through `../MicroTGA-DEM_lambdaDot_common/` (see its `README`).
