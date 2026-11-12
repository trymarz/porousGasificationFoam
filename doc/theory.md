# Theory

What the solver models and where each piece lives in the source. The equations are short-form summaries checked against the code, with the model first described in [Żuk et al. 2022](https://doi.org/10.1016/j.cpc.2022.108407) (CPC 278, 108407). **If this page and the code differ, the code wins** — its comments and the files named below are authoritative.

The paper treats the solid as a fixed bed. The solver now also moves the solid and derives porosity from the solid mass, so the solid equations below differ from the paper's (3a–3c).

## Model overview

Gas and solid coexist in every cell, split by the porosity `ε` (`porosityF`): `ε = 1` is pure gas, `ε = 0` pure solid, in between a porous medium holding both. Phases exchange mass, momentum and energy through source terms from the solid chemistry.

| Symbol | Field / name | Meaning |
|---|---|---|
| `ε` | `porosityF` | gas volume fraction |
| `ρ`, `φ` | `rho`, `phi` | gas density, gas mass flux |
| `Yᵍᵢ` | `<gasName>` | gas species mass fractions |
| `T`, `Tₛ` | `T`, `Ts` | gas and solid temperature |
| `Ymₖ` | `Ym` (per solid component) | solid mass per **cell** volume [kg/m³] |
| `ρₛ`, `Ysₖ` | `rhos`, `Y<solidName>` | solid density, solid mass fractions |
| `Uₛ`, `φₛ` | `Us`, `phiSolid` | solid velocity, limited solid volume flux |

## Per-time-step flow

The loop is `porousGasificationFoam/porousGasificationFoam.C`; each step is an `#include` or call.

1. **Time step** — the Courant (gas) and diffusion (solid) limits give `deltaT` (`setMultiRegionDeltaT.H`). The chemistry's ODE time scale does not limit it. Only with `solidChemistryTimeStepControl true` in `chemistryProperties` (default `false`) does `updateChemistryTimeStep.H` cut `deltaT` further: when, in the last chemistry solve, a specie's mass fraction reached 0 or 1, to about the time it took to get there (`ODESolidHeterogeneousChemistryModel::updateSolidRates`, `updateGasRates`).
2. **DEM coupling** (only with `WITH_YADE`) — particle state is exchanged with YADE; `Us` and `lambda` are rebuilt from it and `lambdaDot` is pushed back (`DEM/lambdaDotModel.C`): a rate YADE integrates, or under `apparentVolume` a factor `r` it applies once.
3. **Radiation** — updates the solid radiative source (`radiation.H`, `radiationModels/`).
4. **Solid region, `pyrolysisZone.evolve()`** (`volPyrolysis::evolveRegion()`), in this order: chemistry ODE and gas-solid exchange, both from the step-start state; solid-flux limit; solid species transport (`SolidSpeciesTransport::solve`); solid enthalpy balance on the new mass, solved for `Ts` (`SolidEnergy::solve`); the DEM `lambdaDot` temperature term; porosity from the solid mass, bed collapse or emptying of flipped cells, their checks, and the solid masks and gate for the next step (`PorosityRecovery`, `volPyrolysis::moveBed`, `checkPorosity`, `updateSolidState`). Each stage, with its kind, the fields it reads and writes and its checks: [solid-step.md](solid-step.md).
5. **Gas continuity** — `rhoEqn.H`.
6. **PIMPLE loop** — momentum `UEqn.H`, species `YEqn.H`, energy `EEqn.H`, pressure `pEqn.H` or `pcEqn.H` (`pimple.consistent()`).
7. **Turbulence** — `turbulence->correct()`.

## Model → input → source

| Model | Input | Source |
|---|---|---|
| Gas continuity, momentum, species, energy | `constant/thermophysicalProperties`, `turbulenceProperties`, `0/` fields | `porousGasificationFoam/{rhoEqn,UEqn,YEqn,EEqn,pEqn,pcEqn}.H` |
| Darcy–Forchheimer resistance | `0/Df`, `constant/porosityProperties` (optional) | `fieldPorosityModel/` |
| Solid chemistry (sources `R`) | `constant/chemistryProperties`, `reactions`, `solidThermophysicalProperties` | `thermophysicalModels/porousSolidChemistryModel/`, `volPyrolysis.C` |
| Solid transport, porosity | `0/Us`, `div(phiSolid)` in `system/fvSchemes` | `solidSpeciesTransport/`, `porosityRecovery/`, `solidFluxLimiter/` |
| Solid energy, gas–solid heat exchange | `solidThermophysicalProperties`, `heatTransferProperties` | `volPyrolysis/solidEnergy/solidEnergy.C`, `thermophysicalModels/solid/heatTransfer/` |
| Radiation | `constant/radiationProperties` | `radiationModels/heterogeneousRadiationModels/` |
| DEM coupling | `constant/lambdaDict` | `porousGasificationMedia/DEM/lambdaDotModel.C` |

`volPyrolysis.C` is under `pyrolysisModels/pyrolysisModel/volPyrolysis/`. Input keys: [case-setup.md](case-setup.md#input-file-reference).

## Equations

`S_ρ,i` is the gas-species mass source from solid reactions (`solidChemistry_->RRg(i)`, kg/m³/s) and `S_ρ = Σᵢ S_ρ,i`. `Rₖ` is the solid mass source of component `k` (`RRs(k)`).

**Gas continuity** (`rhoEqn.H`; paper 2b)

```
∂(ε ρ)/∂t + ∇·φ = (1 − ε) S_ρ
```

**Gas species** (`YEqn.H`; paper 2a). Fractions are clipped at 0 and renormalised to sum to 1 after each solve.

```
∂(ε ρ Yᵢ)/∂t + ∇·(φ Yᵢ) − ∇·(μ_eff ∇Yᵢ) = R_hom,ᵢ + S_ρ,ᵢ
```

The continuity source carries `(1 − ε)`; the species source does not (known discrepancy, [#114](https://github.com/trymarz/porousGasificationFoam/issues/114)).

**Gas momentum** (`UEqn.H` with `fieldPorosityModel`; paper 2d)

```
∂(ρU)/∂t + ∇·(φU) + ∇·R_eff = ρg − ∇p − C·U
C = (μ + f ρ |U| √3 / |Df|) · Df
```

`U` is the superficial velocity: `φ` is the face flux of `ρU` (`compressibleCreatePhi.H`), and `rhoEqn.H` pairs it with `∂(ερ)/∂t`. `Df` is the Darcy resistance tensor (`0/Df`) and `f` the Forchheimer coefficient (`forchheimerCoeff` in `constant/porosityProperties`, default 0, so Darcy only). The paper writes the non-Darcy term in a different form; the code is as above.

**Gas energy** (`EEqn.H`; paper 2c). Solved for sensible enthalpy `h` (or internal energy, depending on the thermo type); `K` is kinetic energy.

```
∂(ε ρ h)/∂t + ∇·(φ h) + ∂(ε ρ K)/∂t + ∇·(φ K) − ε ∂p/∂t − ∇·(α_eff ∇h)
    = ε Q̇_chem + Q_conv + Q_int + S_rad
Q_int = Σ_{S_ρ,i>0} S_ρ,i hₛ,ᵢ(Tₛ) + Σ_{S_ρ,i<0} S_ρ,i hₛ,ᵢ(T)
```

Solved for internal energy `e`, the pressure work `− ε ∂p/∂t` becomes `∇·(φ/ρ p) + p ∂ε/∂t` (from `h = e + p/ρ`, with `φ` the superficial mass flux; see the `EEqn.H` Description). `Q_conv` is the solid-to-gas convective exchange (`heatTransfer()`), `S_rad` the radiation source. `Q_int` (`interfaceEnthalpy`, `GasSolidHeatExchange`) is the sensible enthalpy the interface mass `S_ρ,i` brings with it (#212): products are born at `Tₛ`, consumed agents leave at the gas `T`. It uses the same `S_ρ,i` as `rhoEqn.H` and `YEqn.H`, and is `h` even when solving for `e`.

**Solid chemistry sources** (`Rₖ`, `S_ρ,i`, `S_chem`; paper Eq. 4–6, 10, 12). They are step means of one ODE per cell. What a rate is per, how it is split among species, what the ODE state is and what stays frozen in a step are in the `Description` of `ODESolidHeterogeneousChemistryModel.H` (#209); the code wins.

**Solid species** (`SolidSpeciesTransport::solve`; paper 3a without the transport term)

```
∂Ymₖ/∂t + ∇·(φₛ Ymₖ) = Rₖ
```

`φₛ` is the solid flux after limiting (`solidFluxLimiter`), so Ym stays physical when solid moves.

**Porosity** (`PorosityRecovery::updatePorosity`). Not transported; derived from the solid mass each step. Nearly empty (flipped) cells are cleared only with `bedCollapse`, which moves the column onto them, or with `emptyFlippedCells`, which destroys their mass; both are off by default, and then their solid stays ([solid-step.md](solid-step.md), stages 9–10).

```
1/ρₛ = Σₖ Ysₖ/ρₖ ,    1 − ε = Σₖ Ymₖ/ρₖ
```

**Solid energy** (`SolidEnergy::solve`, after the species step; paper 3c). The stored variable is `H = Σₖ Ymₖ cₚ,ₖ Tₛ` (`solidH`), but the equation is solved for `Tₛ` (`SolidEnergy::solveEnthalpyBalance`):

```
∂(ρcₚ* Tₛ)/∂t + (ρcₚ* Tₛ⁰ − H⁰)/Δt + ∇·(Σₖ φ_Ym,k cₚ,ₖ Tₛ) − ∇·((1 − ε) K_s · A ∇Tₛ)
    = S_chem − Q_conv − Q_agent + S_rad,s
ρcₚ* = Σₖ cₚ,ₖ (Ymₖ − Δt RRsₖ)
Q_agent = Σ_{S_ρ,i<0} |S_ρ,i| (hₛ,ᵢ(Tₛ) − hₛ,ᵢ(T))
```

- `ρcₚ*` (`rhoCpCarrier`) is the heat capacity of the solid before this step's reaction. With Euler the first two terms are `(ρcₚ* Tₛ − H⁰)/Δt`, which is `∂H/∂t − Σₖ RRsₖ cₚ,ₖ Tₛ`: the sensible heat of the composition change, taken implicitly. The first term follows the case's `ddtSchemes` entry; the second is zero unless transport changed the mass.
- The advective enthalpy flux is explicit, and upwind on each specie's own mass flux `φ_Ym,k` (`advectiveEnthalpyFlux`).
- Conduction is implicit, but cut at the bed border (`conductionWithinSolid`): its coefficients are zeroed on every internal, processor and cyclic face where the interpolated `solidPresent` lies strictly between 0 and 1. Its non-orthogonal correction is dropped.

`H` is then stored on the mass that remains.

`A` is the optional `anisotropyK` tensor; `S_chem` is the solid reaction heat (`chemistrySh`), `hr` with all participants at `Tₛ`. `Q_agent` (`agentHeatUp`) heats a consumed agent from the gas `T` to `Tₛ`. Products need no further solid term: the implicit composition-change term already lets the converted solid leave at `Tₛ`. Reference temperatures differ (solid `H` from 0 K, gas `h` from 298.15 K; to be unified in #173); no temperature depends on them.

**Particle size under `lambdaMode apparentVolume`** (`DEM/lambdaDotModels/apparentVolumeLambdaDot/`). With `λ³ₖ = 1/ρ_app,k` the apparent specific volume of specie `k`, and `Ymₖ` taken before this step's solve:

```
V_old = Σₖ Ymₖ λ³ₖ ,    ΔV_chem = Δt Σₖ Rₖ λ³ₖ ,    r = ∛(1 + ΔV_chem/V_old)
λ ← r λ        (in YADE, once per coupling exchange)
```

Only chemistry changes the particle size; solid transport moves particles without resizing them. Every particle in a cell gets the same cell-mean `r`.

## DEM coupling

Only in a `WITH_YADE` build with `constant/yadeProperties` `active true`. YADE starts `porousGasificationFoam` itself (`FoamCoupling::StartFoamSolver`, always with `-parallel`), and the two sides exchange once per solver step, in step 2 of the [per-time-step flow](#per-time-step-flow). OpenFOAM side: `FoamYade` (`submodules/Foam-Yade/pkg/openfoam/coupling/FoamYade/`) and `porousGasificationMedia/DEM/lambdaDotModel.C`. YADE side: `FoamCoupling` (`submodules/Foam-Yade/pkg/openfoam/FoamCoupling.cpp`). Build: [build.md](build.md#yade-dem-coupling).

**Who owns what.** YADE owns the integrated length scale `λ` (`State::lambda_`, default 1). PGF owns only what it sends: the rate `λ̇` or, under `apparentVolume`, a factor `r`. PGF's `lambda` field is cell-averaged diagnostic output; no PGF equation reads it (`lambdaDotModel::updateParticleFields`).

### Exchanged fields

Two per-particle buffers, with strides fixed in the code on both sides (no layout negotiation):

| Slot | YADE → OpenFOAM (`sendBodyData`, `TAG_PRT_DATA` 1002, 11 per body) | Unit |
|---|---|---|
| 0–2 | position | m |
| 3–5 | linear velocity | m/s |
| 6–8 | angular velocity | rad/s |
| 9 | sphere radius | m |
| 10 | `λ` (`State::lambda_`) | as `lambda` (see below) |

| Slot | OpenFOAM → YADE (`sendHydroForceYadeMPI`, `TAG_FORCE` 1005, 7 per body) | Unit |
|---|---|---|
| 0–2 | hydrodynamic force | N |
| 3–5 | hydrodynamic torque | N·m |
| 6 | `λ̇` of the body's cell (`lambdaMode` 0), or `r` (`lambdaMode` 1) | m/s, or – |

- **On the mesh.** Each OpenFOAM rank locates the bodies in its cells. It fills `nParticles`, the cell means `UsDEM` and `lambdaDEM`, and then `Us` and `lambda` from those, smoothed by the `interpolationMode` / `lambdaInterpolationMode` models of `constant/lambdaDict` ([case-setup.md](case-setup.md#constantlambdadict-required-by-with_yade-builds-running-a-dem-case)).
- **Force.** The point-force path (`gaussianInterp` is `false` in `createYadeCoupling.H`): Stokes drag `F = 3π d ν ρ_f (u_f − v_p)` and torque `π d³ ν ρ_f (ω_f − ω_p)`, with `ν`, `ρ_f` from `yadeProperties` (`FoamYade::stokesDragForce`, `stokesDragTorque`). It acts on the particles only: the `uSource` it fills is read by no PGF equation. YADE adds the last received force every DEM step (`setHydroForce`).
- **Other messages.** Once at startup, the mesh bounding box (`TAG_GRID_BBOX`) and the λ mode (`TAG_LAMBDA_MODE`, one int; all OpenFOAM ranks must agree). Every exchange, the found flags (`TAG_SEARCH_RES`), OpenFOAM's `deltaT` (`TAG_FLUID_DT`) and YADE's smallest `O.dt` over its ranks (`TAG_YADE_DT`).
- **Units of λ.** `lambda`, `lambdaDEM` and `lambdaBackgroundValue` are declared in m, and `λ̇` in m/s. In the fixtures' spring model λ starts at 1 and multiplies a rest length, so it acts as a stretch ratio.

### λ modes

`lambdaMode` in `constant/lambdaDict` selects the `LambdaDotCalculationModel` (`DEM/lambdaDotModels/`), and through it the integer `createYadeCoupling.H` sends YADE at startup:

| `lambdaMode` | Sent in slot 6 | YADE applies it | Source |
|---|---|---|---|
| `constant` (default) | rate `λ̇ = lambdaValue` [m/s] | `λ += λ̇·dt` every DEM step (`NewtonIntegrator`) | `constantLambdaDot` |
| `exactDifferential` | rate `λ̇ = (∂λ/∂Tₛ) dTₛ/dt + Σᵢ (∂λ/∂Ymᵢ) Rᵢ` [m/s] | as above | `exactDifferentialLambdaDot.H` (Description) |
| `apparentVolume` | factor `r = ∛(1 + ΔV_chem/V_old)` [–] | `λ ← r λ` once per exchange (`applyLambdaFactor`) | `apparentVolumeLambdaDot.H`; equations [above](#equations) |

Before the exchange, `lambdaDotModel::updateLambdaDot()` sets the neutral value (0 for a rate, 1 for `r`) wherever `porosityF ≥ connectedBelowPorosity`. The comment there: "the value gated and pushed to particles here is the previous step's", because the models assemble the value later in the step, inside the solid solve.

### Which rank sets λ̇ (#229)

A body near a rank boundary lies in the bounding box of several OpenFOAM ranks. Each rank sends a value for every body YADE gave it, and zeros for the bodies it did not find in its cells. YADE takes slot 6 only from the rank whose search found the body (`found[i] > 0`): `setHydroForce` sets `lambdaDot` from that rank alone, and `applyLambdaFactor` multiplies by `r` once per body, "by the one that found it". A body that no rank found keeps its previous `λ̇`.

### Time step (#230)

YADE exchanges every `n` DEM steps (`dataExchangeInterval`, `FoamCoupling::setExchangeInterval`):

- **Rate modes** (`constant`, `exactDifferential`): `n = round(foamDt/yadeDt)`. The run stops (`MPI_Abort`, message `in a lambdaDot rate mode Yade's dt must divide the OpenFOAM deltaT exactly`) unless `n ≥ 1` and `|foamDt/yadeDt − n| ≤ 10⁻⁶ n`. That is, `yadeDt ≤ foamDt` with an integer ratio. YADE integrates `λ̇` every DEM step, so λ then advances by `λ̇·n·yadeDt = λ̇·foamDt` per exchange. Before #230, the fixtures ran at `yadeDt = 5·foamDt`, so λ advanced 5× too fast.
- **`apparentVolume`**: `n = int(foamDt/yadeDt)` when `yadeDt < foamDt`, else 1. `r` is applied once per exchange, so the ratio does not change λ.

The step must be fixed on both sides: `adjustTimeStep no` and a fixed `O.dt` (`tutorials/cases/DEM_common/yadeEndpoint.py` refuses anything else).

### Spring model and engine order (#231)

The springs are not part of YADE or Foam-Yade. They are Python in the DEM fixtures' shared script `tutorials/cases/MicroTGA-DEM_lambdaDot_common/MPI_lambda.py`, the only place λ has a mechanical effect (#206).

- **Topology.** Each sphere is joined to its 6 nearest neighbours (`cKDTree`). `L₀` is their initial distance.
- **Rest length.** `L_target = L₀ · λ̄`, with `λ̄` the mean of the two endpoints' `state.lambda_`. "lambda is read, not integrated, here".
- **Clamps.** `λ̄` is clamped to [0.1, 2.0] for the force only, without writing back into `lambda_`. The spring "freezes" (adds no force) when `L ≤ 0.8 L₀`. It breaks and is removed when the elongation `(L − L₀)/L₀` exceeds 100 %.
- **Force.** Hooke, `F = k (L − L_target)` along the spring, equal and opposite on both ends. `k = c²/(4m)`, with `c = spring_damping = 1.0` and `m = 0.000268`, the relation for critical damping, used only to set `k`. There is no dashpot term; motion is damped by `NewtonIntegrator(damping=0.99)`.
- **Engine order.** `ForceResetter` → collider → `InteractionLoop` → `FoamCoupling` (adds the drag, sets `λ̇` or applies `r`) → `apply_spring_forces()` (every step) → `spring_force_at_newton()` → `NewtonIntegrator` (moves bodies, `λ += λ̇·dt`) → output runners. The script's own reason: "addF is non-permanent, so a force added after Newton is reset unused (#231)". `springForceAtNewton` in `postProcessing/` records the spring force Newton integrates. The `lambdaDotChemistry`, `lambdaDotAltInterp` and `lambdaDotApparentVolume` fixtures assert it is > 0 (`system/regressionInvariants`).

### Limitations

- **The YADE radius, mass and inertia do not follow λ** (#206). Nothing in YADE reads `lambda_` except the fixtures' springs. Slot 9 sends the unchanged radius, and the OpenFOAM side computes the Stokes drag from that diameter.
- **Cross-rank springs** (#231, B4). Every YADE rank computes every spring whose two bodies it can see, and remote copies carry a stale `lambda_`. With 2 or more YADE workers, a spring across ranks is counted 0, 1 or 2 times. The fixtures run `-n 2` (an empty master plus one worker), so it does not occur there.
- **At least 2 OpenFOAM ranks.** The solver is always spawned with `-parallel`, so the case must be decomposed; the fixtures use `numProcOF = 2`.
- **One `r` per cell.** Every particle in a cell gets the cell's `λ̇` or `r`, whatever its own composition.

## Not summarised here

Reaction kinetics, diffusive resistance, reaction enthalpies, heat-transfer correlations and the radiation models are described in the paper (§2.2–2.4) and in the source under `thermophysicalModels/` and `radiationModels/`.

If you find a discrepancy between this page and the code, the code wins — please open an issue or PR.
