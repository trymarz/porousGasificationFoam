# porousGasificationFoam

OpenFOAM solver for reactive flow through porous media with coupled gas-solid physics. Designed for gasification, pyrolysis, and combustion of solid fuels (biomass, coal, char, etc.) in fixed and moving beds.

- License: GNU GPL v3
- OpenFOAM-v2406 (this repository): https://github.com/pjzuk/porousGasificationFoam
- OpenFOAM 8 backport: https://github.com/btuznik/porousGasificationFoam

## Quick Start

### Prerequisites

- OpenFOAM-v2406 (or the foam-extend-4.1 variant)
- MPI (for parallel runs)
- Optional: YADE DEM library (for DEM coupling, set `WITH_YADE=1`)

### Build and Install

```bash
# 1. Source OpenFOAM (adjust path for your installation)
source /path/to/OpenFOAM-v2406/etc/bashrc

# 2. Set porousGasificationFoam environment variables
cd /path/to/porousGasificationFoam
source porousGasificationMediaDirectories

# 3. Build everything
./Allwmake

# Alternative: fine-grained build
./build.sh build --all
```

### Run a Tutorial Case

The gasifier tutorial demonstrates a full fixed-bed gasification simulation:

```bash
cd tutorials/cases/gasifier
./Allrun
```

This runs `blockMesh`, `setFields`, `decomposePar`, and launches `porousGasificationFoam -parallel` on 4 processes.

### Run All Tutorial Cases

```bash
cd tutorials
./TestAllCases.sh
```

### Clean Build

```bash
./Allwclean
# or
./build.sh clean --all
```

## Project Structure

```
porousGasificationFoam/
├── porousGasificationFoam/          # Main solver executable
│   ├── porousGasificationFoam.C     # Time loop (main algorithm)
│   ├── createFields.H               # Field creation
│   ├── createPorosity.H             # Darcy resistance setup
│   ├── createPyrolysisModel.H       # Pyrolysis/solid chemistry setup
│   ├── rhoEqn.H                     # Gas continuity equation
│   ├── UEqn.H                       # Momentum equation (Navier-Stokes + Darcy)
│   ├── YEqn.H                       # Gas species transport
│   ├── EEqn.H                       # Gas energy equation
│   ├── pEqn.H / pcEqn.H             # Pressure correction
│   ├── radiation.H                  # Radiative source term
│   ├── solidRegionDiffusionNo.H     # Solid diffusion stability
│   ├── setMultiRegionDeltaT.H       # Time-step controller
│   └── updateChemistryTimeStep.H    # Chemistry-limited time step
├── porousGasificationMedia/         # Supporting libraries
│   ├── pyrolysisModels/
│   │   └── volPyrolysis/            # Solid-phase evolution engine
│   ├── thermophysicalModels/
│   │   └── porousSolidChemistryModel/
│   │       ├── ODESolidHeterogeneousChemistryModel/  # ODE chemistry solver
│   │       └── basicPorousChemistryModel/             # Base chemistry class
│   ├── radiationModels/             # Heterogeneous P1 / mean-temp radiation
│   ├── fieldPorosityModel/          # Darcy resistance (pZones.addResistance)
│   └── DEM/                         # YADE DEM coupling (optional)
├── tutorials/
│   └── cases/                       # 13 tutorial cases
├── utilities/
│   ├── setPorosity/                 # Creates porosityF and Df fields
│   ├── totalMassPorousGasificationFoam/  # Mass conservation diagnostic
│   ├── bash_utils/                  # Helper scripts
│   └── py_utils/                    # Python post-processing tools
├── build.sh                         # Fine-grained build script
└── doc/Doxygen/                     # Doxygen documentation source
```

## Detailed Physical and Code Structure

### Core Concept: Two-Phase Coexistence

Gas and solid phases coexist in every cell, distinguished by the **porosity** field `porosityF`:

| `porosityF` | Meaning |
|---|---|
| 1.0 | Pure gas (no solid) |
| 0.0 | Pure solid (fully dense) |
| 0 < φ < 1 | Porous medium containing both phases |

The solid mass per unit volume is `rhos = rho * (1 - porosityF)` [kg/m³], where `rho` is the intrinsic solid material density. Mass, momentum, and energy are exchanged between phases through coupling source terms computed by the pyrolysis/chemistry model.

### What Happens in One Time Step (10-Step Algorithm)

The main solver loop in `porousGasificationFoam.C` executes the following sequence:

```
┌─────────────────────────────────────────────────┐
│ 1. Time-step control                            │
│    → Courant (gas), Diffusion (solid), Chemistry │
├─────────────────────────────────────────────────┤
│ 2. Radiation (solid phase)                      │
│    → radiation->correct()                       │
│    → radiationF = radiation->solidSh()          │
├─────────────────────────────────────────────────┤
│ 3. Solid phase evolution                        │
│    → pyrolysisZone.evolve()                     │
│      a. Chemistry ODE integration               │
│      b. Solid species mass conservation         │
│      c. Porosity evolution                      │
│      d. Solid energy equation                   │
├─────────────────────────────────────────────────┤
│ 4. Gas continuity (rhoEqn)                      │
│    → ∂(φ·ρ)/∂t + ∇·(φ·U) = Sρ·(1-φ)           │
├─────────────────────────────────────────────────┤
│ 5. PIMPLE loop (iterative):                     │
│    a. Momentum (UEqn) — NS + Darcy (+Forchheimer)│
│    b. Gas species (YEqn) — advection-diffusion  │
│    c. Gas energy (EEqn) — enthalpy transport    │
│    d. Pressure correction (pEqn)                │
├─────────────────────────────────────────────────┤
│ 6. Turbulence update                            │
└─────────────────────────────────────────────────┘
```

#### Step 1: Time-Step Control

Three stability limits are checked and the minimum time step is selected:

```
maxDeltaTFluid  = maxCo / (CoNum + SMALL)
maxDeltaTSolid  = maxDi / (DiNum + SMALL)
```

- `maxCo` — Courant number for the gas phase (default 5)
- `maxDi` — Diffusion number for the solid phase (default 5000), computed from `K * deltaCoeffs / (Cp * rho)`
- Chemistry time step — returned by the ODE integrator

The actual time step change is damped to avoid oscillations:
```
dt_new = dt_old * min(1.2, min(1+0.1·r_fluid, 1+0.1·r_solid, r_fluid, r_solid))
```

#### Step 2: Radiation

```cpp
radiation->correct();
radiationF = radiation->solidSh();
```

Two heterogeneous radiation models:

| Model | Description |
|---|---|
| `heterogeneousP1` | P1 approximation — accounts for solid-phase absorption, scattering, emission. Solves a diffusion equation for incident radiation. |
| `heterogeneousMeanTemp` | Simplified model using mean temperature. |

Radiative source term `radiationF` [W/m³] is computed based on solid temperature, absorption coefficient `a`, scattering coefficient `as`, and surface layer properties (`borderAs`, `borderL`).

#### Step 3: Solid Phase Evolution (`pyrolysisZone.evolve()`)

This is the heart of the solver, implemented in `volPyrolysis::evolveRegion()` (`volPyrolysis.C:1214`). It performs five sub-steps:

##### 3a. Solid Chemistry ODE Integration

```cpp
timeChem_ = solidChemistry_->solve(t0, deltaT);
```

Integrates per-cell ordinary differential equations for solid species conversion, gas generation, and temperature change. Returns the characteristic chemical time scale. (See detailed chemistry section below.)

##### 3b. Chemical Energy Source

```cpp
chemistrySh_ = solidChemistry_->Sh()();
```

Computes the energy release/absorption [W/m³] from heterogeneous reactions. Two modes:
- `solidReactionEnergyFromEnthalpy = true`: uses heats of formation `hf` of solid and gas species
- `solidReactionEnergyFromEnthalpy = false`: uses specified `heatOfReaction` from reaction definition

##### 3c. Energy to Heat Pyrolysis Gases

```cpp
heatUpGas_ = heatUpGasCalc()();
```

Pyrolysis gases leave the solid at solid temperature `Ts` and must be heated (or cooled) to the gas temperature `Tg` in the energy equation. This term represents `Sρ · Cp_gas · (Ts - Tg)`.

##### 3d. Solid Species Mass Conservation (`solveSpeciesMass()`)

Solves two equations:

1. **Solid bulk density** (advection only):
```
∂ρ_s/∂t = -∇·(ρ_s · Us)
```

2. **Solid species mass fractions** (chemistry + advection):
```
∂(ρLoc · Y_s,i)/∂t = ω_s,i - ∇·(Us · ρLoc · Y_s,i)
```
where `ρLoc = max(ρ · (1-φ), SMALL)`.

After solving all species, mass fractions are renormalized so they sum to 1.

##### 3e. Porosity Evolution (`evolvePorosity()`)

```
∂φ/∂t = RRpor - ∇·(Us · φ)
```
where:
```
RRpor = -Σ(ω_s,i / ρ_s,i)    [1/s]
```

After solving, cells where porosity exceeds `criticalPorosity` (default 0.9999) are identified for the bed-collapse algorithm. If `bedCollapse` is enabled, material from downstream cells is shifted upward to replenish solid mass.

##### 3f. Solid Energy Equation (`solveEnergy()`)

```
ρCp · ∂Ts/∂t = ∇·(K_eff · ∇(Ts)) + Sh_chem - Q_conv - Q_heatUpGas + Q_radiation
```

where:
- `K_eff = K · (1-φ) · anisotropyK` — effective solid thermal conductivity
- `Sh_chem` — reaction heat from chemistry
- `Q_conv = h · SAV · (Ts - Tg)` — gas-solid convective exchange (from `heatTransferProperties`)
- `Q_heatUpGas` — energy to heat pyrolysis gas
- `Q_radiation` — from radiation model

A simplified immersed boundary treatment zeroes out conductive fluxes across the gas-solid interface to avoid spurious heat transfer at porous medium boundaries.

### Solid Chemistry — ODE Integration

The chemistry model is `ODESolidHeterogeneousChemistryModel` in `thermophysicalModels/porousSolidChemistryModel/`.

#### Reaction Rate Laws

Four kinetic rate variants (line 1 of reaction definition):

| Keyword | Rate formula `k(T) =` |
|---|---|
| `irreversibleSolidArrheniusHeterogeneousReaction` | `A · exp(-Ta/T)` (if T ≥ Tcrit, else 0) |
| `irreversibleSolidTemperatureArrheniusHeterogeneousReaction` | `A · T · exp(-Ta/T)` |
| `irreversibleSolidModArrHeterogeneousReaction` | `A · (T-Tcrit)^(2/3) · exp(-Ta/T)` |
| `irreversibleSolidConstHeterogeneousReaction` | `A` (constant) |

The overall forward rate for a reaction is:

```
kf = k(T) · Π(Y_reactant_i^n_i) · ρ_solid   (for solid reactants)
kf = k(T) · Π(Y_reactant_i^n_i) · ρ_gas     (for gas-only reactants)
```

#### Diffusion-Limited Reactions

When `diffusionLimitedReactions = true`, the effective rate is limited by mass transfer:

```
1/k_eff = 1/k_kinetic + Σ(1 / (ST · ρ_g · Y_gas_reactant))
```

where `ST` is the mass transfer coefficient [1/s] from `constant/specieTransferProperties`.

#### Per-Cell ODE System

For each cell containing solid, the following system is solved:

```
d(ρ_s · Y_s,i)/dt = ω_s,i              (i = 1..nSolids)
d(ρ_g · Y_g,j)/dt = ω_g,j              (j = 1..nGases)
dT/dt = -Σ(ω_i · H_i) / Σ(Y_i · Cp_i)   (temperature)
```

The last equation is derived from the energy balance: reaction heat changes temperature via `Cp·dT/dt = -Σ(ω_i · hf_i)` when using enthalpy-based energy, or `Cp·dT/dt = heatOfReaction` when using specified heats.

#### ODE Sub-Cycling Algorithm (`calculateSourceTerms()`)

The ODE system is integrated with adaptive sub-cycling:

```
t = t0
timeLeft = deltaT
while (timeLeft > SMALL):
    tauC_ = solve(specieConcentration_, Ti, p, t, dt_)
    t += dt_
    // Update temperature from reaction enthalpy
    dTi = -Σ(ΔYi·hfi) + heatOfReaction) / (newCp · solidRho) · dt_
    Ti += dTi
    timeLeft -= dt_
    dt_ = min(timeLeft, tauC_)
```

The default ODE solver is `seulex` (implicit extrapolation method), configured in `chemistryProperties`.

#### Mass Partitioning

Two modes controlled by `stoichiometricReactions`:

| Mode | Description |
|---|---|
| `false` (default) | Mass fractions split by stoichiometric coefficient ratios. Total substrates mass = total products mass. |
| `true` | Uses molecular weights to compute mass-conserving partitioning. Accounts for differences in molar masses between reactants and products. |

In both modes, mass is strictly conserved. If a solid substrate converts to both solid products and gas products, the mass ratio between solid and gas products is proportional to the stoichiometric coefficients.

### Porosity Evolution and Bed Motion

#### Porosity Source

The porosity source term represents solid consumption:

```
RRpor = -Σ(ω_s,i / ρ_s,i)
```

The porosity equation is solved with an advection term for moving beds:

```cpp
fvScalarMatrix porosityEqn
(
    fvm::ddt(por)
    ==
    porositySource_
    - fvc::div(Us, por, "div(phiSolid)")
);
```

#### Bed Collapse Model

When `bedCollapse = true` in `pyrolysisProperties` and porosity exceeds `criticalPorosity`:

1. **Detection**: Cells where `porosity > criticalPorosity` AND `(Us · ∇(whereIs)) > 0` (solid moving into the cell) are flagged.
2. **Path tracing**: Starting from each flagged cell, the algorithm follows the advection path opposite to the solid velocity direction, building a chain of cells (a "route").
3. **Material shift**: Properties (porosity, porosityF0, temperature, solid density, solid species mass fractions) are copied from the source cell downward along the route:

```cpp
porosity[destination] = 1 - (1 - porosity[source]) * (V_source / V_destination)
T[destination] = T[source]
rho[destination] = rho[source]
Ys[i][destination] = Ys[i][source]
```

### Gas-Solid Coupling Summary

| Quantity | Solid Equation Source | → Gas Equation Source |
|---|---|---|
| Mass | `∂ρ_s/∂t = -ω_s` (loss) | `Sρ = +ω_s` in `rhoEqn`, `YEqn`, `pEqn` |
| Species `i` | `ω_s,i` in species mass eqn | `Sρ(i) = +ω_s,i` in `YEqn` |
| Energy | `-Sh_chem + Q_conv + Q_heatUpGas - Q_rad` in `solveEnergy()` | `+Q_conv + Q_heatUpGas + radiation->Sh()` in `EEqn` |
| Momentum | — | Darcy resistance `Df·U` (+ optional Forchheimer `F_c·ρ·|U|·√3·D/|D|`) in `UEqn` |
| Volume | Porosity `φ` fills gas space | `φ` multiplies gas-phase volume terms |

Source terms in detail:

- `Srho = Σω_s,i` [kg/m³/s] — total mass transferred solid → gas, limited to `(1 - porosityF)` in equations
- `Srho(i) = ω_g,i` [kg/m³/s] — mass of gas species `i` from pyrolysis
- `heatTransfer = h · SAV · (Ts - Tg)` [W/m³] — convective exchange
- `heatUpGas = Sρ · Cp_gas · (Ts - Tg)` [W/m³] — heating pyrolysis products
- `chemistrySh_` [W/m³] — heterogeneous reaction heat
- `radiationF` [W/m³] — solid radiative source

## Input File Reference

### `constant/chemistryProperties`

Defines solid chemistry configuration — the most important file for reaction kinetics.

```cpp
chemistry           on;

solidChemistryType
{
    solver              solidOde;
    method              ODESolidHeterogeneousChemistryModel;
    solidThermoType     const<constRad<constThermo<constRho>>>;
}

initialChemicalTimeStep 1e-5;

solidReactionEnergyFromEnthalpy false;   // true = use Hf, false = use heatReact
stoichiometricReactions false;           // true = mass-conserving stoichiometry
diffusionLimitedReactions true;          // enable mass-transfer limitation
showRelativeReactionRates false;

solidOdeCoeffs
{
    solver      seulex;                  // ODE integrator
}

species  // Pyrolysis gas names (must match gas species fields in 0/)
(
    CO N2 O2
);

solidReactions
(
    // Format (3 lines per reaction):
    // irreversibleSolidArrheniusHeterogeneousReaction
    // wood + 1.25 O2 = 0.5 ash + 1.75 CO
    // (5.61e9 1.96e4 300 -6.22e6 1.0 1.0)
);
```

**Reaction parameter format**: `(A Ta Tcrit heatOfReaction n1 n2 ...)`

| Parameter | Meaning | Units |
|---|---|---|
| `A` | Pre-exponential factor | varies |
| `Ta` | Activation temperature `Ea/R` | [K] |
| `Tcrit` | Minimum temperature for reaction | [K] |
| `heatOfReaction` | Heat released/absorbed (>0 = exothermic) | [J/kg] |
| `n1, n2, ...` | Reaction order for each LHS species in order | — |

### `constant/solidThermophysicalProperties`

Defines solid components and their thermophysical properties.

```cpp
thermoType solidMixtureThermo<constHeterogeneous>;

solidComponents
(
    ash wood
);

woodCoeffs
{
    transport       { K           0.341; }   // [W/m/K]
    thermodynamics  { Cp          1800;  Hf -1.04e6; }  // [J/kg/K], [J/kg]
    density         { rho         1050; }   // [kg/m³]
};

ashCoeffs { ... }
```

Three thermo variants:

| `solidThermoType` | Cp(T) | Transport | Notes |
|---|---|---|---|
| `constHeterogeneous` | constant | constant | Simplest, adequate for most cases |
| `expoHeterogeneous` | `c0·(T/Tref)^n0` | exponential | T-dependent Cp and K |
| `linearHeterogeneous` | constant | linear | T-dependent K = a·T + b |

### `constant/pyrolysisProperties`

```cpp
active          true;
heterogeneousPyrolysisModel  volPyrolysis;

pyrolysisCoeffs
{
    subintegrateHeatTransfer true;
    bedCollapse             false;       // enable bed motion
    criticalPorosity        0.9999;      // porosity threshold for bed collapse
    replenish               false;       // fuel replenishment model
}
```

### `constant/heatTransferProperties`

```cpp
heatTransferModel constCONV;
Parameters
{
    h       8;        // Heat transfer coefficient [W/m²/K]
    SAV     468;      // Specific surface area [1/m]
}
```

Volumetric heat transfer rate: `Q = h · SAV · (T_gas - T_solid)` [W/m³].

### `constant/specieTransferProperties`

```cpp
specieTransferModel constST;
Parameters
{
    h       0.017;    // Mass transfer coefficient
    SAV     468;      // Specific surface area [1/m]
}
```

Only used when `diffusionLimitedReactions true`.

### `constant/porosityProperties` (optional)

Adds a Forchheimer (quadratic) resistance term to the momentum equation on top of the linear Darcy term. If the file is absent, `forchheimerCoeff` defaults to `0` and only the Darcy term `Df·U` is applied.

```cpp
forchheimerCoeff 3.21e6;
```

Effective momentum sink: `−µ_eff · D · ⟨u⟩  −  F_c · ρ_f · |⟨u⟩| · √3 · D / |D|`. The Forchheimer term matters for high-velocity flow through coarse beds (≳ 1 m/s); for slow flow inside a fine porous medium the linear Darcy term is usually sufficient. Unlike `Df` (a per-cell field), `forchheimerCoeff` is a single scalar applied everywhere.

### `constant/radiationProperties`

```cpp
radiation       on;
heterogeneousRadiationModel  heterogeneousP1;
solverFreq 1;

heterogeneousAbsorptionEmissionModel heterogeneousConstantAbsorptionEmission;
heterogeneousConstantAbsorptionEmissionCoeffs
{
    a               a  [ 0 -1  0 0 0 0 0 ] 0;       // Absorption coefficient [1/m]
    as              as [ 0 -1  0 0 0 0 0 ] 0;       // Scattering coefficient [1/m]
    borderAs        borderAs [ 0 -1  0 0 0 0 0 ] 180; // Surface absorption [1/m]
    E               E  [ 1 -1 -3 0 0 0 0 ] 0.0;     // Emissive power
    borderL         borderL  [ 0 1 0 0 0 0 0 ] 1.5e-3; // Surface layer [m]
}
```

### `system/fvSolution` — Key Settings

```cpp
solvers
{
    Ts    { solver PCG; preconditioner DIC; tolerance 1e-10; } // tight tolerance!
    rhos  { solver PCG; preconditioner DIC; }
}

PIMPLE
{
    momentumPredictor   yes;
    nOuterCorrectors    2;
    nCorrectors         2;
    nNonOrthogonalCorrectors 0;
}
```

The `Ts` (solid temperature) solver requires a tight tolerance (1e-10) for stability.

### `system/fvSchemes` — Key Settings

```cpp
div(phiSolid)     Gauss upwind;   // MUST be upwind for solid advection stability
```

### `system/controlDict` — Key Settings

```cpp
application     porousGasificationFoam;
adjustTimeStep  yes;
maxCo           5;          // Courant number limit (fluid)
maxDi           5000;       // Diffusion number limit (solid)
```

## Required Initial Fields

| Field | Type | Description |
|---|---|---|
| `p` | volScalarField | Pressure [Pa] |
| `U` | volVectorField | Velocity [m/s] |
| `T` | volScalarField | Gas temperature [K] |
| `Ts` | volScalarField | Solid temperature [K] |
| `rhos` | volScalarField | Solid density [kg/m³] |
| `porosityF` | volScalarField | Porosity (0 = solid, 1 = gas) |
| `porosityF0` | volScalarField | Initial porosity (archival copy) |
| `Df` | volTensorField | Darcy resistance tensor |
| `anisotropyK` | volTensorField | Optional, READ_IF_PRESENT. Anisotropic multiplier for the solid thermal conductivity used in the solid energy equation (`K_eff = K · (1−φ) · anisotropyK`). Defaults to the unit tensor if the file is absent. |
| `<gasName>` (e.g. `O2`, `N2`, `CO`) | volScalarField | Gas species mass fractions |
| `Y<solidName>` (e.g. `Ywood`, `Yash`) | volScalarField | Solid species mass fractions |
| `Ydefault` | volScalarField | Default field for unmatched gas species (set to 0) |
| `YsDefault` | volScalarField | Default field for unmatched solid species (set to 0) |

**Notes:**
- `Df` is a tensor field. For isotropic porous media, use a diagonal tensor with large values (e.g. `1e9`) in gas-only regions and smaller values (based on permeability) in porous zones. Use the `setPorosity` utility to create appropriate fields.
- Solid species field names: `Y` + component name from `solidThermophysicalProperties` (e.g. `wood` → `Ywood`).
- Gas species field names match the `species` list in `chemistryProperties`.

## Tips for Preparing New Simulations

Practical notes that are easy to get wrong on a first PGF case.

### Biomass distribution (initial `porosityF`)

Three approaches, in increasing order of complexity:

1. **`setFields`** (with or without `setSet`): the standard OpenFOAM workflow. Specify a `setFieldsDict` that sets `porosityF` inside a cell zone to the desired void fraction and leaves the rest at `1.0`. `setFields` alone is sufficient for simple geometries; pair with `setSet` (batch mode) for more involved selection logic. Other fields (`T`, `Ts`, solid species mass fractions, …) can be initialised the same way. See `tutorials/cases/macroTGA_*` for an example chain (`blockMesh` → `setSet` → `refineHexMesh` → `setFields`).
2. **STL + `setFields`**: for non-trivial bed geometries, build an STL of the porous region in Salome or Blender and feed it to `setSet`/`setFields` via a `surfaceToCell` selector.
3. **`setPorosity` utility**: a code-driven generator (see `utilities/setPorosity/`). The user-editable description of the medium lives in `medium.H`; the tool must be recompiled after each change. Recommended only when scripted parametric sweeps are needed.

### Solid thermophysical properties: true density, not bulk

Entries in `solidThermophysicalProperties` (`rho`, `K`, `Cp`, `Hf`) describe the **pure solid** — i.e. the material at `porosityF = 0`. The macroscopic bulk density inside the bed is then `ρ · (1 − φ)`; PGF reconstructs it from `porosityF` and the intrinsic `rho`. Literature often reports bulk values instead, so be careful: feeding a bulk density into `rho` understates the solid by a factor of `(1 − φ)`. The exception is the `gasifier` tutorial, which deliberately models the entire packed bed at the megascopic scale using bulk wood density (`rho 663` kg/m³ vs ≈ 1050 kg/m³ for true wood); this is a modelling choice consistent with treating the bed as a homogeneous Darcy medium.

### Gas species without JANAF data

Pyrolysis pseudo-species (e.g. lumped `tar`, `targas`) generally have no JANAF coefficients. In `thermo.compressibleGas` pick an existing species from `$FOAM_ETC/thermoData` whose molar mass and `Cp(T)` are the best available proxy for the pseudo-species. Document the substitution in the case to make the assumption auditable later.

### Radiation parameters are the hardest to source

The heterogeneous radiation coefficients (`a`, `as`, `borderAs`, `borderL`, `E`) rarely appear in literature with the right semantics. The recommended workflow is to tune them on a small TGA-like case so that the simulated heating rate matches an experiment, then reuse the tuned values across geometries that share the same surface chemistry and pellet morphology. The macroTGA tutorials are sized for exactly this calibration step.

### Time-step coupling between gas and solid chemistry

Gas-phase reactions are typically orders of magnitude faster than heterogeneous solid reactions. When both are active, gas chemistry dominates the chemistry-limited time step and can make slow processes (e.g. gasification of a whole bed) very expensive. Practical implications:

- For slow heterogeneous processes, prefer chemistry mechanisms with the minimum gas-phase detail needed for the result you care about.
- `maxCo`, `maxDi`, and `initialChemicalTimeStep` in `controlDict` / `chemistryProperties` interact; tightening any one of them can mask the actual bottleneck. Check the solver log for which limit is binding before tuning.

### Mesh and parallel execution

- Run `setPorosity` (if used) **before** `decomposePar`; it operates on the reconstructed mesh.
- The `totalMassPorousGasificationFoam` utility also requires a reconstructed case. Run `reconstructPar` first, or operate on the un-decomposed run.
- `paraView -builtin` can visualise the decomposed processor directories directly without reconstruction — useful for very large cases.
- Re-running a regression baseline must use the same `numberOfSubdomains` as the original; floating-point sensitivity to decomposition is real (see `applications/test/regression/README.md`).

## Tutorial Cases

All 13 cases under `tutorials/cases/`:

| Case | Description | Notable features |
|---|---|---|
| `microTGA/` | Micro-scale TGA (3 variants: MeanTemp, P1, Template) | Radiation on, template reference |
| `macroTGA_688K/` | Macro-scale TGA at 688 K | — |
| `macroTGA_688K_fine/` | Macro-scale TGA at 688 K, refined mesh | — |
| `macroTGA_879K/` | Macro-scale TGA at 879 K | — |
| `macroTGA_879K_fine/` | Macro-scale TGA at 879 K, refined mesh | — |
| `macroTGA_experimentalData/` | TGA with experimental data comparison | — |
| `gasifier/` | Fixed-bed gasifier | 4-proc parallel, wedge geometry |
| `flatPlate/` | Flat plate reactive flow | — |
| `biomassPressureDrop/` | Pressure drop through biomass bed | — |
| `charOnlyMove/` | Moving char bed (no reactions) | Bed motion test |
| `MicroTGA-DEM/` | DEM-coupled micro TGA | Requires YADE |
| `DEM_UsInterp_*/` | DEM solid velocity interpolation tests | Requires YADE |

## Utilities

### `setPorosity`

Creates `porosityF` and `Df` fields based on porous medium parameters (particle diameter, tortuosity, permeability). Run before the simulation:

```bash
cd <caseDir>
setPorosity
```

### `totalMassPorousGasificationFoam`

Post-processing utility that integrates solid-state mass over the domain at each stored time step. Useful for checking mass conservation. The integrated quantity is:

```
m(t) = ∫_Ω  ⟨ρ⟩_s · (1 − φ)  dx
```

where `⟨ρ⟩_s` is the intrinsic solid density and `φ = porosityF` is the void fraction.

```bash
cd <caseDir>
totalMassPorousGasificationFoam
```

Writes `totalMass.txt` with two columns (time, mass). The utility operates on a reconstructed case — run `reconstructPar` first if the case was decomposed for a parallel run.

## Build System

### Simple build

```bash
./Allwmake      # Build everything
./Allwclean     # Clean everything
```

### Fine-grained control with build.sh

```bash
./build.sh build --libs-only           # Libraries only
./build.sh build --apps-only           # Solver and utilities only
./build.sh build --radiationModels --pyrolysisModels --porousGasificationFoam  # Specific targets
./build.sh build --no-DEM              # Skip DEM (if YADE not available)
./build.sh build --all --dry-run       # Preview commands
./build.sh clean --all                 # Clean all
```

### Build targets

| Target | Type | Dependency |
|---|---|---|
| DEM | library | — (if `WITH_YADE=1`) |
| fieldPorosityModel | library | — |
| radiationModels | library | solid thermo |
| thermophysicalModels | library suite | — |
| pyrolysisModels | library | all above |
| porousGasificationFoam | executable | all libraries |
| utilities | executable suite | — |

### YADE DEM coupling

```bash
WITH_YADE=1 ./build.sh build --all
```

Requires YADE installed with the OpenFOAM coupling module.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `Cannot find library` | `porousGasificationMediaDirectories` not sourced | Run `source porousGasificationMediaDirectories` |
| `undefined symbol` | Library version mismatch or incomplete rebuild | `./build.sh clean --all && ./build.sh build --all` |
| Simulation crashes immediately | Missing initial fields | Check `solidComponents` match `0/Y*` files |
| Negative temperatures | Too large time step, or aggressive reaction parameters | Reduce `deltaT`, `maxCo`, `maxDi`, or `initialChemicalTimeStep` |
| Porosity exceeds 1 or goes below 0 | Bed collapse too aggressive | Reduce `deltaT` or adjust `criticalPorosity` |
| Mass fractions do not sum to 1 | Missing `Ydefault` field or gas species | Check all gas species have corresponding `0/` files |
| Slow convergence in pressure | Tight PIMPLE settings | Increase `nCorrectors` or relax `p` tolerance |
| Parallel: `decomposePar` fails | Missing decompose constraints | Ensure `Ts`, `porosityF`, `porosityF0` use `calculated` or `zeroGradient` BCs |

## For AI Coding Agents

This repository ships an `AGENTS.md` at the root with a thin operating layer for AI coding agents (Claude Code, OpenCode, and any other tool that follows the `AGENTS.md` / `CLAUDE.md` conventions). A one-line `CLAUDE.md` `@import`s it so both tools load the same content.

`AGENTS.md` contains only agent-specific guidance — workflow rules, verification habits, and a short "current dev status" block that humans are encouraged to keep up to date. Anything useful to a human contributor stays in this README; the split is intentional and one-directional.

If you collaborate with an agent on this codebase, edit `AGENTS.md` to record what is in flux and what direction the next chunk of work is heading. Agents are instructed to flag contradictions between `AGENTS.md` and the current code if they spot any, so a slightly out-of-date file fails loud rather than silent.

## Documentation

Doxygen documentation:

```bash
cd doc/Doxygen
./Allwmake
```

Output: `$WM_PROJECT_DIR/doc/Doxygen/html/index.html`

Requires `doxygen` and `graphviz`:

```bash
sudo apt-get install doxygen graphviz
```

## Citation

If you use this solver, please cite:

> Żuk, P. J., Tużnik, B., Rymarz, T., Kwiatkowski, K., Dudyński, M., Galeazzo, F. C., & Krieger Filho, G. C. (2022). OpenFOAM solver for thermal and chemical conversion in porous media. *Computer Physics Communications*, 278, 108407.

## Contributors

Paweł Jan Żuk, Bartosz Tużnik, Tadeusz Rymarz, Zhiwar, Kamil Kwiatkowski, Marek Dudyński, Flavio C. C. Galeazzo, Guenther C. Krieger Filho, Filip Mróz (foam-extend-4.1 to v2406 port)
