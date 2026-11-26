# Code map

Where each part of the [model](theory.md) lives in the source, and the order in which the solver runs the parts in one time step. The next pages zoom in on two steps of that order: the [solid step](solid-step.md) and the [DEM exchange](dem-exchange.md). **If this page and the code differ, the code wins.**

## Where each model lives

| Model | Configured in | Implemented in |
|---|---|---|
| Gas flow | `thermophysicalProperties`, `turbulenceProperties` | `{rho,U,Y,E,p,pc}Eqn.H` |
| Gas reactions | `combustionProperties`, `chemistryProperties`, `reactions` | OpenFOAM `CombustionModel` |
| Darcy–Forchheimer | `0/Df`, `porosityProperties` | `fieldPorosityModel/` |
| Solid chemistry | `chemistryProperties`, `reactions`, `solidThermophysicalProperties` | `thermophysicalModels/porousSolidChemistryModel/` |
| Diffusion limit | `specieTransferProperties` | `thermophysicalModels/solid/specieTransfer/` |
| Solid transport, porosity | `0/Us`, `pyrolysisProperties`, `system/fvSchemes` | `solidSpeciesTransport/`, `solidFluxLimiter/`, `porosityRecovery/` |
| Solid energy | `solidThermophysicalProperties`, `0/anisotropyK` | `solidEnergy/` |
| Gas–solid heat | `heatTransferProperties` | `thermophysicalModels/solid/heatTransfer/`, `gasSolidHeatExchange/` |
| Radiation | `radiationProperties` | `radiationModels/heterogeneousRadiationModels/` |
| DEM coupling | `lambdaDict`, `yadeProperties` | `DEM/`, `demCoupling/` |

- **Paths.** Inputs are in `constant/` unless they start with `0/` or `system/`; their keys: [case reference](case-reference.md). The `*Eqn.H` files are in `porousGasificationFoam/`. Model folders are under `porousGasificationMedia/`; the solid-step ones (`solidSpeciesTransport/` to `demCoupling/` without a prefix above) are under its `pyrolysisModels/pyrolysisModel/volPyrolysis/`, where `volPyrolysis.C` runs the solid step.
- **Optional inputs.** `porosityProperties` (default Darcy only), `0/anisotropyK` (default identity), and `specieTransferProperties`, which is read only when `diffusionLimitedReactions` is on in `chemistryProperties`.
- **Solid transport.** `pyrolysisProperties` → `pyrolysisCoeffs` holds the limiter and bed switches. The `div(phiSolid)` entry is in `system/fvSchemes`.
- **DEM coupling.** Needs a `WITH_YADE` build; see [DEM coupling](theory-DEM.md).

## Per-time-step flow

The time loop in `porousGasificationFoam/porousGasificationFoam.C`:

```
for each time step:
    choose Δt              # gas Courant and solid diffusion limits;
                           # optionally capped by the solid chemistry
    exchange with DEM      # WITH_YADE builds only
    update radiation
    solid step:                                     # solid-step.md stage
        chemistry and gas–solid heat exchange,      # 1–4
            from the step-start state
        limit the solid flux                        # 5
        transport the solid species                 # 6
        solve the solid energy for T_s              # 7
        classify cells, then the T_s regime rules   # 7a–7b
        DEM λ temperature term                      # 8
        porosity from the solid mass;               # 9–12
            collapse or empty the bed
        report the mass budget                      # 13
    gas continuity
    PIMPLE loop:
        momentum, species, energy
        pressure correctors
    turbulence
```

1. **Time step** — the gas Courant and solid diffusion limits set Δt (`setMultiRegionDeltaT.H`); with local time stepping (LTS) `setRDeltaT.H` sets a local Δt instead. With `adjustTimeStep` and `solidChemistryTimeStepControl` both on, `updateChemistryTimeStep.H` also caps Δt by about the time a specie's mass fraction took to reach 0 or 1 in the last chemistry solve.
2. **DEM exchange** — `lambdaDotModel::updateLambdaDot()` (`DEM/lambdaDotModel.C`) gates the values to send, `pgfDemCoupler::exchange()` swaps them for the DEM body data, and `updateParticleFields()` rebuilds `Us` and `lambda` from it. PGF sends the rate `λ̇`, or the factor `r` under `apparentVolume`. Step by step: [DEM exchange](dem-exchange.md).
3. **Radiation** — updates the solid radiative source (`radiation.H`).
4. **Solid step** — `pyrolysisZone.evolve()` (`volPyrolysis::evolveRegion()`) runs the stages in the order shown. Each stage, with the fields it reads and writes and its checks: [solid-step.md](solid-step.md).
5. **Gas continuity** — `rhoEqn.H`.
6. **PIMPLE loop** — `UEqn.H`, `YEqn.H`, `EEqn.H`, then `pEqn.H`, or `pcEqn.H` when `consistent` is on.
7. **Turbulence** — `turbulence->correct()`, gated by `pimple.turbCorr()`.

## Gas–solid energy terms

The terms of [Gas–solid exchange → Energy](theory.md#gassolid-exchange) and where the code computes them. `GasSolidHeatExchange` and `SolidEnergy` are in `volPyrolysis/gasSolidHeatExchange/` and `volPyrolysis/solidEnergy/`; their file `Description` blocks give the formulas as implemented.

| Term | Code |
|---|---|
| `Q_conv`, with or without `subintegrateHeatTransfer` | `GasSolidHeatExchange::heatTransfer()` |
| Donor choice by the sign of `S_ρ,i` | `GasSolidHeatExchange::updateInterfaceEnthalpy` |
| `Q_prod`, `Q_reac` (gas `EEqn.H`) | fields `gasProductEnthalpyAtTs`, `gasReactantEnthalpyAtTg` |
| `Q_reac,s` (solid energy) | field `heatUpGasReactantsToTs` |
| The solid's half of a gas product | `SolidEnergy::carrierHeatCapacity`, `SolidEnergy::storeH`, in `SolidEnergy::solveEnthalpyBalance` |
| `S_chem` | `chemistrySh`, from the solid chemistry's `Sh()` (`volPyrolysis.C`); `solidReactionEnergyFromEnthalpy` is read in `ODESolidHeterogeneousChemistryModel` |

## Related pages

- [Theory](theory.md): the model these files implement.
- [Solid step](solid-step.md): step 4, stage by stage.
- [DEM exchange](dem-exchange.md): step 2, the exchange with YADE.
- [All documentation](README.md)
