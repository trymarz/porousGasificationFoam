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

1. **Time step** — Courant (gas), diffusion (solid) and chemistry limits give one `deltaT` (`setMultiRegionDeltaT.H`, `updateChemistryTimeStep.H`).
2. **DEM coupling** (only with `WITH_YADE`) — particle state is exchanged with YADE; `Us` and `lambda` are rebuilt from it and `lambdaDot` is pushed back (`DEM/lambdaDotModel.C`).
3. **Radiation** — updates the solid radiative source (`radiation.H`, `radiationModels/`).
4. **Solid region, `pyrolysisZone.evolve()`** (`volPyrolysis::evolveRegion()`), in this order: chemistry ODE; solid-flux limit; solid temperature equation and enthalpy transport (`SolidEnergy::solve`); solid species transport (`SolidSpeciesTransport::solve`); `Ts` recovery after the reaction (`recoverTemperatureAfterReaction`); porosity recovery and bed collapse (`PorosityRecovery::recover`).
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

`Df` is the Darcy resistance tensor (`0/Df`) and `f` the Forchheimer coefficient (`forchheimerCoeff` in `constant/porosityProperties`, default 0, so Darcy only). The paper writes the non-Darcy term in a different form; the code is as above.

**Gas energy** (`EEqn.H`; paper 2c). Solved for sensible enthalpy `h` (or internal energy, depending on the thermo type); `K` is kinetic energy.

```
∂(ε ρ h)/∂t + ∇·(φ h) + ∂(ε ρ K)/∂t + ∇·(φ K) − ∂p/∂t − ∇·(α_eff ∇h)
    = ε Q̇_chem + Q_conv + Q_heatUp + S_rad
Q_heatUp = cp (Tₛ − T) S_ρ
```

`Q_conv` is the solid-to-gas convective exchange (`heatTransfer()`), `S_rad` the radiation source.

**Solid species** (`SolidSpeciesTransport::solve`; paper 3a without the transport term)

```
∂Ymₖ/∂t + ∇·(φₛ Ymₖ) = Rₖ
```

`φₛ` is the solid flux after limiting (`solidFluxLimiter`), so Ym stays physical when solid moves.

**Porosity** (`PorosityRecovery::recover`). Not transported; derived from the solid mass each step, then cells that are nearly empty are cleared (bed collapse).

```
1/ρₛ = Σₖ Ysₖ/ρₖ ,    1 − ε = Σₖ Ymₖ/ρₖ
```

**Solid energy** (`SolidEnergy::solve`, `recoverTemperatureAfterReaction`; paper 3c). With `H = (Σₖ Ymₖ) cₚ,ₛ Tₛ`:

```
∂(Σₖ Ymₖ cₚ,ₛ Tₛ)/∂t − ∇·((1 − ε) K_s · A ∇Tₛ) = S_chem − Q_conv − Q_heatUp + S_rad,s
∂H/∂t + ∇·(φ_Ym hₛ) = 0        (enthalpy advected with the solid mass flux, upwind)
Tₛ = H / (Σₖ Ymₖ cₚ,ₛ)
```

`A` is the optional `anisotropyK` tensor; `S_chem` is the solid reaction heat (`chemistrySh`).

## Not summarised here

Reaction kinetics, diffusive resistance, reaction enthalpies, heat-transfer correlations and the radiation models are described in the paper (§2.2–2.4) and in the source under `thermophysicalModels/` and `radiationModels/`.

If you find a discrepancy between this page and the code, the code wins — please open an issue or PR.
