# DEM exchange

What happens in step 2 of the [per-time-step flow](theory.md#per-time-step-flow), when PGF and YADE exchange data, and how the tutorial fixtures use λ. The idea and the body size models: [DEM coupling](theory-DEM.md). **If this page and the code differ, the code wins.**

## Running and code

Running it needs a `WITH_YADE` build ([build.md](build.md#yade-dem-coupling)) and `active true` in `constant/yadeProperties`. `pgfDemRun` (`tutorials/cases/DEM_common/`) starts YADE and the solver, always with `-parallel`, in one `mpirun`; the exchange runs once per PGF time step.

YADE owns each body's λ (default 1); PGF's `lambda` field is only a cell average written for output.

**Code.** PGF side: `porousGasificationMedia/DEM/pgfDemCoupler/` and `DEM/lambdaDotModel.C`. YADE side: the `PgfCoupling` engine (`pkg/openfoam/PgfCoupling.cpp` in trymarz/yade-pgf, the submodule `submodules/yade-pgf`). Both build the same protocol core, [PgfDemBridge](https://github.com/trymarz/PgfDemBridge) (`submodules/PgfDemBridge`, its banner in `src/PgfDemBridge.hpp`), and neither includes the other's headers. A `yadeProperties` with a `coupler` entry other than `pgf` stops the solver (`porousGasificationFoam/createDEMFields.H`).

## λ modes in the exchange

The mode also sets the integer the handshake sends YADE:

| `lambdaMode` | Sent down | YADE applies it | Source |
|---|---|---|---|
| `constant` (default) | rate `λ̇ = lambdaValue` [m/s] | `λ += λ̇·dt` every DEM step (`NewtonIntegrator`) | `constantLambdaDot` |
| `exactDifferential` | rate `λ̇ = (∂λ/∂Tₛ) dTₛ/dt + Σᵢ (∂λ/∂Ymᵢ) Rᵢ` [m/s] | as above | `exactDifferentialLambdaDot.H` (Description) |
| `apparentVolume` | factor `r = ∛(1 + ΔV_chem/V_old)` [–] | `λ ← r λ` once per exchange (`PgfCoupling::apply`) | `apparentVolumeLambdaDot.H`; [body size](theory-DEM.md#body-size-under-lambdamode-apparentvolume) |

Before the exchange, `lambdaDotModel::updateLambdaDot()` sets the neutral value (0 for a rate, 1 for `r`) wherever `porosityF ≥ connectedBelowPorosity`. The comment there: "the value gated and pushed to particles here is the previous step's", because the models assemble the value later in the step, inside the solid solve.

Under `apparentVolume` (`DEM/lambdaDotModels/apparentVolumeLambdaDot/`), `rhoApparent` is required. `r = 1` in nearly empty cells (`V_old < minSolidVolume`); at full burnout the ratio is floored at `minVolumeRatio`, with a warning. Both keys are optional in `lambdaDict`.

## Exchanged fields

| Stage | MPI call | Per body | Content |
|---|---|---|---|
| open, once | `Allgather` on all ranks | – | magic, protocol version (2), side, thread level, `lambdaMode`, strides 7/1; each OpenFOAM rank's mesh bounding box |
| step header | `Allreduce` | – | rebuild and stop flags; a rebuild makes a new neighbour graph from the DEM ranks' edges |
| bodies up | `Neighbor_alltoallv`, each DEM rank → each OpenFOAM rank whose box holds the body's centre | 7 | position [m], velocity [m/s], `λ` [m] |
| values down | `Neighbor_alltoallv`, back | 1 | `λ̇` [m/s] or `r` [–] in the same order, NaN where that rank did not find the body |
| control | `Bcast` from the first OpenFOAM rank | – | `deltaT`, and whether this was the solver's last step |

- **On the mesh.** Each OpenFOAM rank locates the bodies in its cells. It fills `nParticles`, the cell means `UsDEM` and `lambdaDEM`, and then `Us` and `lambda` from those, smoothed by the `interpolationMode` / `lambdaInterpolationMode` models of `constant/lambdaDict` ([case-setup.md](case-setup.md#constantlambdadict-required-by-with_yade-builds-running-a-dem-case)).
- **Stops by name** on a version, stride, thread-level or `lambdaMode` mismatch, on OpenFOAM ranks disagreeing on `lambdaMode`, and on `PgfCoupling.expectLambdaMode` ≠ the case's mode. A DEM script without `PgfCoupling` leaves the solver waiting in the handshake.
- **End.** The solver's last step ends YADE (`PgfCoupling: OpenFOAM ran its last step; the DEM run ends at iter N`); a YADE run that ends first stops the solver. An MPI error on either side aborts both.
- **Erased bodies.** YADE applies `λ̇` or `r` once per exchange, looking each body up by id with `BodyContainer::exists`. A body erased with `mp.bodyErase` is skipped, so a stale list cannot be dereferenced.

## Which rank sets λ̇ (#229)

A body near a rank boundary lies in the bounding box of several OpenFOAM ranks. YADE sends it to each of them; a rank that did not find the body in its cells returns NaN for it, and YADE takes the value of the lowest rank that found it. A body that no rank found keeps its previous `λ̇` (`PgfCoupling::apply`), and `PgfCoupling.nLost` counts it.

## Time step (#230)

YADE exchanges every `n` DEM steps (`dataExchangeInterval`, `PgfCoupling::setExchangeInterval`):

- **Rate modes** (`constant`, `exactDifferential`): `n = round(foamDt/yadeDt)`. The run stops (`MPI_Abort`, message `in a lambdaDot rate mode Yade's dt must divide the OpenFOAM deltaT exactly`) unless `n ≥ 1` and `|foamDt/yadeDt − n| ≤ 10⁻⁶ n`. That is, `yadeDt ≤ foamDt` with an integer ratio. YADE integrates `λ̇` every DEM step, so λ then advances by `λ̇·n·yadeDt = λ̇·foamDt` per exchange. Before #230, the fixtures ran at `yadeDt = 5·foamDt`, so λ advanced 5× too fast.
- **`apparentVolume`**: `n = int(foamDt/yadeDt)` when `yadeDt < foamDt`, else 1. `r` is applied once per exchange, so the ratio does not change λ.

The solver's `controlDict` is the one place that sets the run's time. The Yade scripts read it through `tutorials/cases/DEM_common/foamTime.py`: `O.dt = deltaT / DEM_SUBSTEPS` with a whole number of substeps, outputs every `writeInterval`, and no step count of their own, since the solver's last step ends YADE.

## Spring model and engine order (#231)

The springs are not part of YADE or yade-pgf. They are Python in the DEM fixtures' shared script `tutorials/cases/MicroTGA-DEM_lambdaDot_common/MPI_lambda.py`, the only place λ has a mechanical effect (#206).

- **Topology.** Each sphere is joined to its 6 nearest neighbours (`cKDTree`). `L₀` is their initial distance.
- **Rest length.** `L_target = L₀ · λ̄`, with `λ̄` the mean of the two endpoints' `state.lambda_`. "lambda is read, not integrated, here".
- **Clamps.** `λ̄` is clamped to [0.1, 2.0] for the force only, without writing back into `lambda_`. The spring "freezes" (adds no force) when `L ≤ 0.8 L₀`. It breaks and is removed when the elongation `(L − L₀)/L₀` exceeds 100 %.
- **Force.** Hooke, `F = k (L − L_target)` along the spring, equal and opposite on both ends. `k = c²/(4m)`, with `c = spring_damping = 1.0` and `m = 0.000268`, the relation for critical damping, used only to set `k`. There is no dashpot term; motion is damped by `NewtonIntegrator(damping=0.99)`.
- **Engine order.** `ForceResetter` → collider → `InteractionLoop` → `PgfCoupling` (sets `λ̇` or applies `r`) → `apply_spring_forces()` (every step) → `spring_force_at_newton()` → `NewtonIntegrator` (moves bodies, `λ += λ̇·dt`) → output runners. The script's own reason: "addF is non-permanent, so a force added after Newton is reset unused (#231)". `springForceAtNewton` in `postProcessing/` records the spring force Newton integrates. The `lambdaDotChemistry`, `lambdaDotAltInterp` and `lambdaDotApparentVolume` fixtures assert it is > 0 (`system/regressionInvariants`).

## Limitations

- **Cross-rank springs** (#231, B4). Every YADE rank computes every spring whose two bodies it can see, and remote copies carry a stale `lambda_`. With 2 or more YADE workers, a spring across ranks is counted 0, 1 or 2 times. The fixtures run `-n 2` (an empty master plus one worker), so it does not occur there.
- **Always decomposed.** The solver always runs with `-parallel`, so even one OpenFOAM rank needs a one-piece `processor0`; the fixtures use 2.
- Physical limitations of the coupling: [DEM coupling → Limitations](theory-DEM.md#limitations).

## Related pages

- [DEM coupling](theory-DEM.md): the idea and the body size models.
- [Theory](theory.md): the gas–solid model and the per-time-step flow.
- [All documentation](README.md)
