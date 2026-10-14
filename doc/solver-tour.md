# Solver tour

*A tour, not a reference. Equations, units, and algorithm choices live in the source files that implement them — `Description` blocks in the file banner, block comments above the relevant function, and inline narrative inside. This section narrates the flow and points at where to look.*

## Two-Phase Coexistence

Gas and solid coexist in every cell, distinguished by the porosity field `porosityF` ∈ [0, 1]: 1.0 is pure gas, 0.0 is pure solid, in-between is a porous medium containing both phases. Solid bulk density is `rhos = rho · (1 − porosityF)`, where `rho` is the intrinsic solid material density. Mass, momentum, and energy are exchanged between phases via coupling source terms computed by the pyrolysis/chemistry model.

## Per-Time-Step Tour

The main loop is in `porousGasificationFoam/porousGasificationFoam.C`. Each piece of work is pulled in via an `#include`, so the file reads top-to-bottom as a sequence. The steps below cite the include or function that does the work:

1. **Time-step control** — Courant (gas), diffusion (solid), and chemistry timescale are combined into one stable `deltaT`. See `setMultiRegionDeltaT.H` and `updateChemistryTimeStep.H`.
2. **DEM coupling** (compiled only when `WITH_YADE` is defined) — per-particle state is exchanged with YADE, then the mesh-level fields are rebuilt from it: the solid velocity `Us` from `UsDEM`, and the particle length scale `lambda` from `lambdaDEM`, each a raw per-cell average Laplace-smoothed into adjacent solid cells. Finally `lambdaDot` (the rate PGF owns, assembled by `volPyrolysis`) is pushed back onto the particles, and YADE integrates it into the `lambda` this step read. See `lambdaDotModel::updateLambdaDot()` and `lambdaDotModel::updateParticleFields()` in `porousGasificationMedia/DEM/lambdaDotModel.C`, and `constant/lambdaDict` in the Input File Reference for the dictionary that selects all of it.
3. **Radiation** — heterogeneous radiation model (`heterogeneousP1` or `heterogeneousMeanTemp`) updates the solid radiative source term. See `porousGasificationFoam/radiation.H` and `porousGasificationMedia/radiationModels/`.
4. **Solid phase evolution** — the heart of the solver: per-cell chemistry ODE, porosity evolution (with optional bed-collapse), solid species mass-concentration (`Ym`) transport, and the solid energy equation, in that order. See `volPyrolysis::evolveRegion()` in `porousGasificationMedia/pyrolysisModels/pyrolysisModel/volPyrolysis/volPyrolysis.C`.
5. **Gas continuity** — gas-phase density update with the solid-to-gas mass source. See `porousGasificationFoam/rhoEqn.H`.
6. **PIMPLE loop** — momentum (`UEqn.H`) with Darcy/Forchheimer porous resistance, gas species (`YEqn.H`), gas energy (`EEqn.H`), and pressure correction (`pEqn.H` or `pcEqn.H` depending on `pimple.consistent()`).
7. **Turbulence correction** — `turbulence->correct()` in the main loop.

## Where to Look for What

| Question | Where to look |
|---|---|
| What equation does this step solve? Which terms, which units? | `Description` block in the file banner and the comment block above the relevant `evolve*` / `solve*` / `*Eqn` function in the corresponding `.C`/`.H`. |
| Which input dictionary keys does X read? | [Input File Reference](case-setup.md#input-file-reference). |
| What initial fields does a case need? | [Required Initial Fields](case-setup.md#required-initial-fields). |
| Where does this field get constructed? | `porousGasificationFoam/createFields.H` for solver fields; `createDEMFields.H` for DEM-coupled fields. |
| How does the solver layout map to OpenFOAM modules? | [Repository layout](../README.md#repository-layout). |

If you find a discrepancy between this tour and what the code does, the code wins — please open an issue or PR.
