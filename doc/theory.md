# Theory

What the solver models and where each piece lives in the source. The equations are short-form summaries checked against the code, with the model first described in [Żuk et al. 2022](https://doi.org/10.1016/j.cpc.2022.108407) (CPC 278, 108407); reaction kinetics, diffusive resistance, reaction enthalpies, heat-transfer correlations and the radiation models are described there (§2.2–2.4) and in the source. **If this page and the code differ, the code wins** — please open an issue or PR.

In the paper the solid stays in place, and its porosity changes through a chemistry source term. Here the solid can move with the velocity `Us`, and porosity is recomputed each step from the solid mass in the cell. The solid equations below therefore differ from the paper's (3a–3c).

## Model overview

Each cell holds gas and solid in proportions set by the porosity `ε` (`porosityF`): `ε = 1` is pure gas, `ε = 0` pure solid, and anything in between is a porous medium. The solid chemistry passes mass and energy between solid and gas. Momentum goes one way only: the solid slows the gas through the Darcy–Forchheimer resistance, while the gas does not push the solid, whose velocity `Us` is given in `0/Us` or comes from DEM ([the idea behind the DEM coupling](theory-DEM.md#the-idea)). Gas released by the solid brings no momentum with it.

## Notation

| Symbol | Field | Meaning |
|---|---|---|
| `ε` | `porosityF` | gas volume fraction |
| `ρ_g` | `rho` | gas density |
| `φ_g` | `phi` | gas mass flux |
| `Y_g,i` | `<gasName>` | gas species mass fractions |
| `T_g`, `T_s` | `T`, `Ts` | gas, solid temperature |
| `Ymₖ` | `Ym` | solid mass per **cell** volume [kg/m³], one per solid specie |
| `Ysₖ` | `Y<solidName>` | solid mass fractions |
| `ρ_s,k` | `rho` | skeletal density of solid specie `k` |
| `ρ_s` | `rhos` | skeletal density of the solid mixture |
| `ρ_app,k` | `rhoApparent` | apparent density of solid specie `k` |
| `U_s` | `Us` | solid velocity |
| `φ_s` | `phiSolid` | limited solid volume flux |

- **Densities.** Skeletal is the material without its pores; apparent is a particle's mass over its envelope volume, pores included. Both are per-specie entries in `solidThermophysicalProperties`; `rhoApparent` is optional and only `lambdaMode apparentVolume` reads it. The mixture's skeletal density is `1/ρ_s = Σₖ Ysₖ/ρ_s,k`, and its bulk density `ρ_s (1 − ε)`.

## Models at a glance

| Model | Configured in | Implemented in |
|---|---|---|
| Gas flow | `thermophysicalProperties`, `turbulenceProperties` | `{rho,U,Y,E,p,pc}Eqn.H` |
| Gas reactions | `combustionProperties`, `chemistryProperties`, `reactions` | OpenFOAM `CombustionModel` |
| Darcy–Forchheimer | `0/Df`, `porosityProperties` | `fieldPorosityModel/` |
| Solid chemistry | `chemistryProperties`, `reactions`, `solidThermophysicalProperties` | `porousSolidChemistryModel/` |
| Diffusion limit | `specieTransferProperties` | `specieTransfer/` |
| Solid transport, porosity | `0/Us`, `pyrolysisProperties`, `system/fvSchemes` | `solidSpeciesTransport/`, `solidFluxLimiter/`, `porosityRecovery/` |
| Solid energy | `solidThermophysicalProperties`, `0/anisotropyK` | `solidEnergy/` |
| Gas–solid heat | `heatTransferProperties` | `heatTransfer/`, `gasSolidHeatExchange/` |
| Radiation | `radiationProperties` | `heterogeneousRadiationModels/` |
| DEM coupling | `lambdaDict`, `yadeProperties` | `DEM/`, `demCoupling/` |

- **Paths.** Inputs are in `constant/` unless they start with `0/` or `system/`. The `*Eqn.H` files are in `porousGasificationFoam/`. Model folders are under `porousGasificationMedia/`, and the solid-step ones under its `pyrolysisModels/pyrolysisModel/volPyrolysis/`. `volPyrolysis.C` there runs the solid step. Keys: [case-setup.md](case-setup.md#input-file-reference).
- **Gas reactions.** These are the `R_hom,i` and `Q̇_chem` terms. Each enters multiplied by `ε` (`YEqn.H`, `EEqn.H`).
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
    solid step:
        chemistry and gas–solid heat exchange, from the step-start state
        limit the solid flux
        transport the solid species
        solve the solid energy for T_s
        classify cells: connected / dispersed / trace
        DEM λ temperature term
        porosity from the solid mass; collapse or empty the bed
    gas continuity
    PIMPLE loop:
        momentum, species, energy
        pressure correctors
    turbulence
```

1. **Time step** — the gas Courant and solid diffusion limits set Δt (`setMultiRegionDeltaT.H`); with local time stepping (LTS) `setRDeltaT.H` sets a local Δt instead. With `adjustTimeStep` and `solidChemistryTimeStepControl` both on, `updateChemistryTimeStep.H` also caps Δt by about the time a specie's mass fraction took to reach 0 or 1 in the last chemistry solve.
2. **DEM exchange** — particle data from YADE rebuild `Us` and `lambda`; `lambdaDot` goes back to YADE (`DEM/lambdaDotModel.C`, [DEM coupling](theory-DEM.md)).
3. **Radiation** — updates the solid radiative source (`radiation.H`).
4. **Solid step** — `pyrolysisZone.evolve()` (`volPyrolysis::evolveRegion()`) runs the stages in the order shown. Each stage, with the fields it reads and writes and its checks: [solid-step.md](solid-step.md).
5. **Gas continuity** — `rhoEqn.H`.
6. **PIMPLE loop** — `UEqn.H`, `YEqn.H`, `EEqn.H`, then `pEqn.H`, or `pcEqn.H` when `consistent` is on.
7. **Turbulence** — `turbulence->correct()`, gated by `pimple.turbCorr()`.

## Equations

Colours mark the kind of term: ${\color{blue}\text{storage}}$, ${\color{orange}\text{advection}}$, ${\color{green}\text{diffusion}}$, ${\color{red}\text{gas–solid exchange}}$, ${\color{purple}\text{other sources}}$.

### Gas

**Continuity** (`rhoEqn.H`; paper 2b)

$$
{\color{blue}\frac{\partial(\varepsilon\rho_g)}{\partial t}} + {\color{orange}\nabla\cdot\varphi_g} = {\color{red}S_\rho}
$$

**Species** (`YEqn.H`; paper 2a). Fractions are clipped at 0 and renormalised to sum to 1 after each solve.

$$
{\color{blue}\frac{\partial(\varepsilon\rho_g Y_{g,i})}{\partial t}} + {\color{orange}\nabla\cdot(\varphi_g Y_{g,i})} - {\color{green}\nabla\cdot(\mu_{\mathrm{eff}}\nabla Y_{g,i})} = {\color{purple}\varepsilon R_{\mathrm{hom},i}} + {\color{red}S_{\rho,i}}
$$

`R_hom,i` is the gas-phase reaction rate per unit gas volume, hence the `ε` (#226). The interface sources `S_ρ` and `S_ρ,i` are per unit total volume and carry none (#125).

**Momentum** (`UEqn.H` with `fieldPorosityModel`; paper 2d)

$$
{\color{blue}\frac{\partial(\rho_g U_g)}{\partial t}} + {\color{orange}\nabla\cdot(\varphi_g U_g)} + {\color{green}\nabla\cdot R_{\mathrm{eff}}} = {\color{purple}\rho_g g - \nabla p} - {\color{red}C\cdot U_g}
$$

$$
C = \left(\mu + \frac{f\rho_g|U_g|\sqrt{3}}{|D_f|}\right) D_f
$$

`U_g` is the superficial velocity: `φ_g` is the face flux of `ρ_g U_g` (`compressibleCreatePhi.H`), and `rhoEqn.H` pairs it with `∂(ερ_g)/∂t`. `D_f` is the Darcy resistance tensor (`0/Df`) and `f` the Forchheimer coefficient (`forchheimerCoeff` in `constant/porosityProperties`, default 0, so Darcy only). The paper writes the non-Darcy term in a different form; the code is as above.

**Energy** (`EEqn.H`; paper 2c). Solved for sensible enthalpy `h_g` (or internal energy, depending on the thermo type); `K` is kinetic energy.

$$
\begin{aligned}
&{\color{blue}\frac{\partial(\varepsilon\rho_g h_g)}{\partial t} + \frac{\partial(\varepsilon\rho_g K)}{\partial t}} + {\color{orange}\nabla\cdot(\varphi_g h_g) + \nabla\cdot(\varphi_g K)} - {\color{purple}\varepsilon\frac{\partial p}{\partial t}} - {\color{green}\nabla\cdot(\alpha_{\mathrm{eff}}\nabla h_g)} \\
&\quad = {\color{purple}\varepsilon\dot Q_{\mathrm{chem}}} + {\color{red}Q_{\mathrm{conv}} + Q_{\mathrm{prod}} + Q_{\mathrm{reac}} + S_{\mathrm{rad}}}
\end{aligned}
$$

Solved for internal energy `e`, the pressure work `− ε ∂p/∂t` becomes `∇·(φ_g/ρ_g p) + p ∂ε/∂t` (from `h = e + p/ρ`; see the `EEqn.H` Description).

### Gas–solid exchange

**Mass.** Gas and solid exchange mass only through the solid reactions. A solid reaction may have solid and gas species on both sides: solid and gas reactants turn into solid and gas products. For example, in gasification, char reacts with a gasifying agent such as O₂ or CO₂ to give gases; in pyrolysis, wood gives char and volatiles. Each reaction must conserve mass; this is checked when it is read.

The solid chemistry reports this per cell as three rates, each in kg per m³ of cell per second:

- `R_k` (code: `RRs(k)`): how fast solid species `k` is made (> 0) or consumed (< 0). It drives the solid species equation.
- `S_ρ,i` (code: `RRg(i)`): how fast gas species `i` enters the gas as a product (> 0) or leaves the gas as a reactant (< 0). It is 0 for gas species that take part in no solid reaction. It drives the gas species equation.
- `S_ρ = Σᵢ S_ρ,i` (code: `Srho`): the net gas mass gained. It drives the gas continuity equation.

All three come from one chemistry ODE per cell, integrated over the time step at the start of the solid step; they are the mean rates over that step. The gas and solid equations then use the same values, so both sides see one consistent exchange. For more detail, such as what each rate is normalised by, how it is split among species and which quantities stay fixed while the ODE is integrated, see the `Description` at the top of `ODESolidHeterogeneousChemistryModel.H` (#209; paper Eq. 4–6, 10, 12).

**Energy.** Gas and solid each have their own temperature, `T_g` and `T_s`. Energy passes between them by convection, with the mass that crosses the interface, and as radiation. `GasSolidHeatExchange` computes the convection and interface terms once per step, from the step-start temperatures, in cells whose solid is connected at the start of the step (its `Description` has the details). Gas and solid read the same stored values, so what one phase loses the other gains in the same step:

| Route | Gas | Solid |
|---|---|---|
| Convection | $+Q_{\mathrm{conv}}$ | $-Q_{\mathrm{conv}}$ |
| Gas products | $+Q_{\mathrm{prod}}$ | leaves with the converted mass |
| Gas reactants | $+Q_{\mathrm{reac}}$ (< 0) | $-Q_{\mathrm{reac},s}$ |

With `frozenTemperature` (formerly `equilibrium`, in `pyrolysisCoeffs`) no energy equation is solved at all: both temperatures stay at their initial values, each phase its own, and all exchange terms are zero.

*Convection.* $Q_{\mathrm{conv}} = h_v\,(T_s - T_g)$ (`heatTransfer()`), where `h_v` [W/m³/K] is the volumetric heat transfer coefficient from the model chosen in `heatTransferProperties`; it is zero where there is no solid. With `subintegrateHeatTransfer` the code uses the exact two-temperature relaxation over the step instead, which stays stable for large `h_v`.

*Mass crossing the interface.* Gas that crosses carries its sensible enthalpy `h_i` at the temperature of the phase it leaves, the donor. The sign of `S_ρ,i` picks the donor (`updateInterfaceEnthalpy`):

- A gas product (`S_ρ,i > 0`) is born in the solid and enters the gas at `T_s`.
- A gas reactant (`S_ρ,i < 0`) leaves the gas at `T_g`. The solid then warms it to `T_s`, where it reacts.

$$
Q_{\mathrm{prod}} = \sum_{S_{\rho,i}>0} S_{\rho,i}\,h_i(T_s), \qquad
Q_{\mathrm{reac}} = \sum_{S_{\rho,i}<0} S_{\rho,i}\,h_i(T_g)
$$

$$
Q_{\mathrm{reac},s} = \sum_{S_{\rho,i}<0} |S_{\rho,i}|\,\bigl(h_i(T_s) - h_i(T_g)\bigr)
$$

- `Q_prod` (`gasProductEnthalpyAtTs`) and `Q_reac` (`gasReactantEnthalpyAtTg`) go into the gas energy equation. Both are `h` even when the gas solves for `e`: entering mass brings its flow work.
- `Q_reac,s` (`heatUpGasReactantsToTs`) is taken from the solid energy equation.

*The solid's half of a gas product.* The solid has no explicit product term. Its storage terms use the pre-reaction mass (`carrierHeatCapacity`), and `H` is then stored on the mass that remains (`storeH`). Together they equal $\frac{dH}{dt} - \sum_k R_k c_{p,k} T_s$, so the converted solid leaves with its sensible heat at `T_s` (`SolidEnergy::solveEnthalpyBalance`). An explicit term would count it twice.

*Reaction heat.* It is booked once, in the solid, as `S_chem` (`chemistrySh`), with every participant at `T_s`. `solidReactionEnergyFromEnthalpy` in `chemistryProperties` (default `true`) picks how it is computed:

- `false`: `S_chem` is each reaction's `heatReact` times its rate. `heatReact` must then be the heat of reaction with all participants at `T_s`; `Q_reac,s` is what brings the gas reactants there.
- `true`: $S_{\mathrm{chem}} = -\sum_k R_k\,h_k(T_s) - \sum_i S_{\rho,i}\,h_{a,i}(T_s)$, with `h_a,i` the gas absolute enthalpy (formation plus sensible). Then $S_{\mathrm{chem}} - Q_{\mathrm{reac},s}$ is the solid's open-system balance, with gas reactants in at `T_g` and products out at `T_s`; `Q_reac,s` only splits it into two terms.

For char oxidation, C + ½O₂ → CO, the steps are:

```mermaid
flowchart LR
    A["O₂ in the gas, at T_g"] -->|"leaves the gas: Q_reac"| B["O₂ at the surface, at T_g"]
    B -->|"warmed by the solid: Q_reac,s"| C["C + ½O₂ at T_s"]
    C -->|"reacts at T_s: S_chem, into the solid"| D["CO at T_s"]
    D -->|"enters the gas: Q_prod"| E["CO in the gas, at T_s"]
```

By Hess's law the direct route from O₂ at `T_g` to CO at `T_s` gives the same total. The code books it in steps so that each has one sender and one receiver.

*Why the source is at the donor's temperature.* The gas energy equation is conservative: it solves for the energy in the cell, `ερ_g h_g` (`fvm::ddt(porosityRho, he) + fvm::div(phi, he)`, `EEqn.H`). Mass added by `S_ρ` without a matching energy source would arrive with `h = 0`, which for sensible enthalpy is 298.15 K. That was the case before #213; the old `heatUpGas` term only partly corrected it. In the non-conservative form, `ρ_g Dh_g/Dt`, the same omission is silent: new mass takes the cell's `h`, as if born at `T_g`.

- OpenFOAM's compressible solvers (`rhoPimpleFoam`, `reactingFoam`) use the conservative form. `twoPhaseEulerFoam` subtracts the numerical continuity error, `fvm::Sp(contErr, he)` (`EEqns.H`).
- If such a term is ever added to PGF's `EEqn.H`, its `contErr` must exclude `Σᵢ S_ρ,i`, or gas products are again born at `T_g`.

*Reference temperatures.* The gas `h_i` counts from 298.15 K, the solid `H` from 0 K. The offset is absorbed in `heatReact` (it is defined at `T_s`) or, in enthalpy mode, in the `−c_p,k T_std` term of the solid `h_k(T)`; no temperature depends on it. It cancels exactly only for constant `c_p` (#173).

*Radiation.* `S_rad` and `S_rad,s` are the gas and solid radiation sources (`radiation.H`).

### Solid

**Motion.** No equation solves for `U_s`. It is prescribed in `0/Us` (fixed in time) or, with DEM, interpolated from `UsDEM`, the raw cell mean of the particle velocities (`interpolationMode` in `constant/lambdaDict`; [DEM coupling](theory-DEM.md#the-idea)). The solid actually moves with the limited volume flux

$$
\varphi_s = L_f\,(\mathbf{U}_s\cdot\mathbf{S}_f), \qquad 0 \le L_f \le 1
$$

where `S_f` is the face area vector and `L_f` the factor from `solidFluxLimiter`, chosen so no cell gives more solid than it holds or takes more than it has room for. This keeps `Ym` non-negative.

**Species** (`SolidSpeciesTransport::solve`; paper Eq. 3a plus solid advection, which the paper does not have). `Ym_k` is the mass of solid species `k` per m³ of cell. It changes through the solid reactions (`R_k`) and, when the solid moves, through the solid flux `φ_s`:

$$
{\color{blue}\frac{\partial\,\mathrm{Ym}_k}{\partial t}} + {\color{orange}\nabla\cdot(\varphi_s\,\mathrm{Ym}_k)} = {\color{red}R_k}
$$

**Porosity** (`PorosityRecovery::updatePorosity`). Not transported; derived from the solid mass each step, so both chemistry and solid motion change it. Handling of nearly empty cells: [solid-step.md](solid-step.md), stages 9–10.

$$
1-\varepsilon = \sum_k \frac{\mathrm{Ym}_k}{\rho_{s,k}}
$$

**Energy** (`SolidEnergy::solve`, after the species step; paper 3c). The solid enthalpy `H = Σₖ Ym_k c_p,k T_s` (`solidH`) is stored, advected with the solid and conducted through it:

$$
{\color{blue}\frac{\partial H}{\partial t}} + {\color{orange}\nabla\cdot\Bigl(\sum_k \varphi_{\mathrm{Ym},k}\,c_{p,k}T_s\Bigr)} - {\color{green}\nabla\cdot\bigl((1-\varepsilon)K_s A\,\nabla T_s\bigr)} = {\color{purple}S_{\mathrm{chem}}} {\color{red}\;-\;Q_{\mathrm{conv}} - Q_{\mathrm{reac},s} + S_{\mathrm{rad},s}}
$$

`A` is the optional `anisotropyK` tensor; `S_chem` is the solid reaction heat (`chemistrySh`), with all participants at `T_s`. The equation is solved for `T_s`; how the storage term handles this step's composition change: [solid-step.md](solid-step.md), stage 7. Exchange terms and reference temperatures: [Gas–solid exchange](#gassolid-exchange).

## DEM coupling

Only in a `WITH_YADE` build with `constant/yadeProperties` `active true`. DEM replaces the prescribed `U_s` and nothing else in the equations above. In return PGF sends each particle how its length scale λ changes, from the solid chemistry. The idea, the λ modes and the exchange: [theory-DEM.md](theory-DEM.md).

## Related pages

- [Solid step](solid-step.md): the solid step, stage by stage.
- [DEM coupling](theory-DEM.md): the YADE coupling.
- [Setting up your own case](case-setup.md): required fields, input files, tutorial cases, tips.
- [All documentation](README.md)
