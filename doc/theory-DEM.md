# DEM coupling

PGF can run coupled to YADE, a discrete-element (DEM) code that moves individual particles. This needs a `WITH_YADE` build ([build.md](build.md#yade-dem-coupling)) and `active true` in `constant/yadeProperties`. You start YADE, and YADE launches `porousGasificationFoam` itself, always in parallel. The two exchange data once per PGF time step, in step 2 of the [per-time-step flow](theory.md#per-time-step-flow).

**Who owns what.**
- YADE moves the particles and owns each particle's length scale `λ` (default 1).
- PGF sends back only how `λ` should change: a rate `λ̇`, or, under `apparentVolume`, a factor `r`. PGF's `lambda` field is a cell average written for output, and no PGF equation reads it.
- PGF builds the solid velocity `U_s` from the particle velocities.

**Code.** PGF side: `FoamYade` (`submodules/Foam-Yade/pkg/openfoam/coupling/FoamYade/`) and `DEM/lambdaDotModel.C`. YADE side: `FoamCoupling` (`submodules/Foam-Yade/pkg/openfoam/FoamCoupling.cpp`).

## Exchanged fields

Each exchange, per particle:

| Slots | YADE → PGF | Unit |
|---|---|---|
| 0–2 | position | m |
| 3–5 | velocity | m/s |
| 6–8 | angular velocity | rad/s |
| 9 | radius | m |
| 10 | `λ` | m |

| Slots | PGF → YADE | Unit |
|---|---|---|
| 0–2 | force | N |
| 3–5 | torque | N·m |
| 6 | `λ̇` or `r` | m/s or – |

- **Buffers.** YADE sends 11 values per body (`sendBodyData`, tag 1002) and PGF sends 7 (`sendHydroForceYadeMPI`, tag 1005). Both sides hard-code these layouts.
- **On the mesh.** Each OpenFOAM rank locates the bodies in its cells. It fills `nParticles`, the cell means `UsDEM` and `lambdaDEM`, and then `Us` and `lambda` from those, smoothed by the `interpolationMode` / `lambdaInterpolationMode` models of `constant/lambdaDict` ([case-setup.md](case-setup.md#constantlambdadict-required-by-with_yade-builds-running-a-dem-case)).
- **Force.** The point-force path (`gaussianInterp` is `false` in `createYadeCoupling.H`): Stokes drag `F = 3π d ν ρ_f (u_f − v_p)` and torque `π d³ ν ρ_f (ω_f − ω_p)`, with `ν`, `ρ_f` from `yadeProperties` (`FoamYade::stokesDragForce`, `stokesDragTorque`). It acts on the particles only: the `uSource` it fills is read by no PGF equation. YADE adds the last received force every DEM step (`setHydroForce`).
- **Other messages.** Once at startup, the mesh bounding box (`TAG_GRID_BBOX`) and the λ mode (`TAG_LAMBDA_MODE`, one int; all OpenFOAM ranks must agree). Every exchange, the found flags (`TAG_SEARCH_RES`), OpenFOAM's `deltaT` (`TAG_FLUID_DT`) and YADE's smallest `O.dt` over its ranks (`TAG_YADE_DT`).
- **Units of λ.** `lambda`, `lambdaDEM` and `lambdaBackgroundValue` are declared in m, and `λ̇` in m/s. In the fixtures' spring model λ starts at 1 and multiplies a rest length, so it acts as a stretch ratio.

## Particle size under `lambdaMode apparentVolume`

(`DEM/lambdaDotModels/apparentVolumeLambdaDot/`). In this mode a particle grows or shrinks with the apparent volume of the solid in its cell. One kilogram of solid specie `k` fills `1/ρ_app,k` of particle volume, pores included (`rhoApparent` in `solidThermophysicalProperties`, required here). `V_old` is the volume the solid in a cell takes up before this step's reactions, and `ΔV_chem` is how much the reactions change it over the step:

$$
V_{\mathrm{old}} = \sum_k \frac{\mathrm{Ym}_k}{\rho_{\mathrm{app},k}}, \qquad \Delta V_{\mathrm{chem}} = \Delta t \sum_k \frac{R_k}{\rho_{\mathrm{app},k}}
$$

A particle's volume scales with `λ³`, so PGF sends YADE the cube root of the volume ratio, and YADE scales `λ` by it once per exchange:

$$
r = \sqrt[3]{1 + \Delta V_{\mathrm{chem}}/V_{\mathrm{old}}}, \qquad \lambda \leftarrow r\,\lambda
$$

- Only chemistry changes the size. Solid transport moves particles without resizing them.
- Every particle in a cell gets the same `r`.
- `r = 1` in nearly empty cells (`V_old < minSolidVolume`). At full burnout the ratio is floored at `minVolumeRatio`, with a warning. Both keys are optional in `lambdaDict`.

## λ modes

`lambdaMode` in `constant/lambdaDict` selects the `LambdaDotCalculationModel` (`DEM/lambdaDotModels/`), and through it the integer `createYadeCoupling.H` sends YADE at startup:

| `lambdaMode` | Sent in slot 6 | YADE applies it | Source |
|---|---|---|---|
| `constant` (default) | rate `λ̇ = lambdaValue` [m/s] | `λ += λ̇·dt` every DEM step (`NewtonIntegrator`) | `constantLambdaDot` |
| `exactDifferential` | rate `λ̇ = (∂λ/∂Tₛ) dTₛ/dt + Σᵢ (∂λ/∂Ymᵢ) Rᵢ` [m/s] | as above | `exactDifferentialLambdaDot.H` (Description) |
| `apparentVolume` | factor `r = ∛(1 + ΔV_chem/V_old)` [–] | `λ ← r λ` once per exchange (`applyLambdaFactor`) | `apparentVolumeLambdaDot.H`; [particle size](#particle-size-under-lambdamode-apparentvolume) |

Before the exchange, `lambdaDotModel::updateLambdaDot()` sets the neutral value (0 for a rate, 1 for `r`) wherever `porosityF ≥ connectedBelowPorosity`. The comment there: "the value gated and pushed to particles here is the previous step's", because the models assemble the value later in the step, inside the solid solve.

## Which rank sets λ̇ (#229)

A body near a rank boundary lies in the bounding box of several OpenFOAM ranks. Each rank sends a value for every body YADE gave it, and zeros for the bodies it did not find in its cells. YADE takes slot 6 only from the rank whose search found the body (`found[i] > 0`): `setHydroForce` sets `lambdaDot` from that rank alone, and `applyLambdaFactor` multiplies by `r` once per body, "by the one that found it". A body that no rank found keeps its previous `λ̇`.

## Time step (#230)

YADE exchanges every `n` DEM steps (`dataExchangeInterval`, `FoamCoupling::setExchangeInterval`):

- **Rate modes** (`constant`, `exactDifferential`): `n = round(foamDt/yadeDt)`. The run stops (`MPI_Abort`, message `in a lambdaDot rate mode Yade's dt must divide the OpenFOAM deltaT exactly`) unless `n ≥ 1` and `|foamDt/yadeDt − n| ≤ 10⁻⁶ n`. That is, `yadeDt ≤ foamDt` with an integer ratio. YADE integrates `λ̇` every DEM step, so λ then advances by `λ̇·n·yadeDt = λ̇·foamDt` per exchange. Before #230, the fixtures ran at `yadeDt = 5·foamDt`, so λ advanced 5× too fast.
- **`apparentVolume`**: `n = int(foamDt/yadeDt)` when `yadeDt < foamDt`, else 1. `r` is applied once per exchange, so the ratio does not change λ.

The step must be fixed on both sides: `adjustTimeStep no` and a fixed `O.dt` (`tutorials/cases/DEM_common/yadeEndpoint.py` refuses anything else).

## Spring model and engine order (#231)

The springs are not part of YADE or Foam-Yade. They are Python in the DEM fixtures' shared script `tutorials/cases/MicroTGA-DEM_lambdaDot_common/MPI_lambda.py`, the only place λ has a mechanical effect (#206).

- **Topology.** Each sphere is joined to its 6 nearest neighbours (`cKDTree`). `L₀` is their initial distance.
- **Rest length.** `L_target = L₀ · λ̄`, with `λ̄` the mean of the two endpoints' `state.lambda_`. "lambda is read, not integrated, here".
- **Clamps.** `λ̄` is clamped to [0.1, 2.0] for the force only, without writing back into `lambda_`. The spring "freezes" (adds no force) when `L ≤ 0.8 L₀`. It breaks and is removed when the elongation `(L − L₀)/L₀` exceeds 100 %.
- **Force.** Hooke, `F = k (L − L_target)` along the spring, equal and opposite on both ends. `k = c²/(4m)`, with `c = spring_damping = 1.0` and `m = 0.000268`, the relation for critical damping, used only to set `k`. There is no dashpot term; motion is damped by `NewtonIntegrator(damping=0.99)`.
- **Engine order.** `ForceResetter` → collider → `InteractionLoop` → `FoamCoupling` (adds the drag, sets `λ̇` or applies `r`) → `apply_spring_forces()` (every step) → `spring_force_at_newton()` → `NewtonIntegrator` (moves bodies, `λ += λ̇·dt`) → output runners. The script's own reason: "addF is non-permanent, so a force added after Newton is reset unused (#231)". `springForceAtNewton` in `postProcessing/` records the spring force Newton integrates. The `lambdaDotChemistry`, `lambdaDotAltInterp` and `lambdaDotApparentVolume` fixtures assert it is > 0 (`system/regressionInvariants`).

## Minimal coupler (`coupler pgf`, #271)

`coupler` in `constant/yadeProperties` selects the OpenFOAM half: `foamYade` (default, everything above) or `pgf`, the PGF-owned `porousGasificationMedia/DEM/pgfYadeCoupler/`. Its YADE half is the `PgfCoupling` engine in the Foam-Yade fork (`pkg/openfoam/PgfCoupling.cpp`); the fixtures' `MPI_lambda.py` builds it when `yadeProperties` says `coupler pgf;`. `pgfYadeCoupler` includes no Foam-Yade header, so the wire format is written down in both files' banners and checked at startup:

| Message | Direction | Per body | Content |
|---|---|---|---|
| HELLO 2701, BBOX 2702 | each OpenFOAM rank → every YADE rank, once | – | magic, protocol version (1), strides 8/2, `lambdaMode`, rank count; mesh bounding box |
| ACK 2703 | YADE rank 0 → each OpenFOAM rank, once | – | magic, version, status, first worker rank, worker count |
| COUNT 2711, DATA 2712 | each YADE worker → each OpenFOAM rank | 8 | body id, position, velocity, `λ` |
| REPLY_COUNT 2713, REPLY 2714 | each OpenFOAM rank → each YADE worker | 2 | body id, `λ̇` or `r`, only for bodies whose centre that rank found |
| FOAM_DT 2721 | OpenFOAM rank 0 → YADE rank 0 | – | `deltaT` |

- **Stops by name** on a version, stride or `lambdaMode` mismatch, on OpenFOAM ranks disagreeing on `lambdaMode`, and on `PgfCoupling.expectLambdaMode` ≠ the case's mode. A legacy peer is caught too: YADE sees tag 1001 from `FoamYade`; OpenFOAM gets no ACK from `FoamCoupling` within `handshakeTimeout` (`yadeProperties`, default 60 s).
- **No drag or torque.** Only the fields the solver uses cross: radius, angular velocity, `SHARED_ID` and `YADE_DT` are gone. Stokes drag moved the spheres, so `Us` differs from the `foamYade` path (an intended physics change).
- **#229, #230 kept.** Only a rank that found a body replies for it, and YADE takes the first reply per body. The rate-mode `foamDt/yadeDt` integer check is the same as `FoamCoupling::setExchangeInterval`.
- **Erased bodies.** YADE applies `λ̇` or `r` once per exchange, looking each body up by id with `BodyContainer::exists`. A body erased with `mp.bodyErase` is skipped, so a stale list cannot be dereferenced.
- **Same mesh side.** Both paths feed `lambdaDotModel` the same per-rank particle list (`DEM/demParticle.H`), so `nParticles`, `UsDEM`, `lambdaDEM`, `Us` and `lambda` are built the same way.

## Limitations

- **The YADE radius, mass and inertia do not follow λ** (#206). Nothing in YADE reads `lambda_` except the fixtures' springs. Slot 9 sends the unchanged radius, and the OpenFOAM side computes the Stokes drag from that diameter.
- **Cross-rank springs** (#231, B4). Every YADE rank computes every spring whose two bodies it can see, and remote copies carry a stale `lambda_`. With 2 or more YADE workers, a spring across ranks is counted 0, 1 or 2 times. The fixtures run `-n 2` (an empty master plus one worker), so it does not occur there.
- **At least 2 OpenFOAM ranks.** The solver is always spawned with `-parallel`, so the case must be decomposed; the fixtures use `numProcOF = 2`.
- **One `r` per cell.** Every particle in a cell gets the cell's `λ̇` or `r`, whatever its own composition.

## Related pages

- [Theory](theory.md): the gas–solid model.
- [All documentation](README.md)
