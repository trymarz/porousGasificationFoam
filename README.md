# porousGasificationFoam

OpenFOAM solver for reactive flow through porous media with coupled
gas-solid physics. Designed for gasification, pyrolysis, and combustion
of solid fuels (biomass, coal, char, etc.) in fixed and moving beds.

- License: GNU GPL v3
- OpenFOAM-v2406 (this repository): https://github.com/pjzuk/porousGasificationFoam
- OpenFOAM 8 backport: https://github.com/btuznik/porousGasificationFoam

---

## Citing

If you use this solver, please cite:

> P. J. Zuk, B. Tużnik, T. Rymarz, K. Kwiatkowski, M. Dudyński,
> F. C. C. Galeazzo, G. C. Krieger Filho, F. Mróz
> *OpenFOAM solver for thermal and chemical conversion in porous media*
> Submitted to Computer Physics Communications

---

## Contributors

Paweł Jan Żuk, Bartosz Tużnik, Tadeusz Rymarz, Zhiwar, Kamil Kwiatkowski,
Marek Dudyński, Flavio C. C. Galeazzo, Guenther C. Krieger Filho,
Filip Mróz (foam-extend-4.1 to v2406 port)

---

## Table of Contents

1. [Key Concepts](#key-concepts)
2. [Quick Start](#quick-start)
3. [Build System](#build-system)
4. [Case Structure](#case-structure)
5. [Input File Reference](#input-file-reference)
6. [Required Initial Fields](#required-initial-fields)
7. [Tutorial Cases](#tutorial-cases)
8. [Utilities](#utilities)
9. [Troubleshooting](#troubleshooting)
10. [Documentation](#documentation)

---

## Key Concepts

### Two-phase coupling

Gas and solid phases coexist in every cell. They are distinguished by the
**porosity** field `porosityF`:

| `porosityF` | Meaning |
|---|---|
| 1.0 | Pure gas (no solid) |
| 0.0 | Pure solid (fully dense) |
| 0 < φ < 1 | Porous medium containing both phases |

Mass, momentum, and energy are exchanged between the phases through
coupling source terms computed by the pyrolysis/chemistry model.

### What happens in one time step

1. **Solid phase evolves** — solid chemistry integrates ODEs for solid
   species conversion, porosity evolves as solid is consumed, solid
   temperature is solved (conduction, radiation, reaction heat, gas-solid
   convection), and mass/energy source terms for the gas phase are computed.
2. **Gas continuity** — density equation with pyrolysis mass source.
3. **PIMPLE loop** (iterative pressure-velocity coupling):
   - Momentum: Navier-Stokes + Darcy porous resistance
   - Gas species transport: advection-diffusion with homogeneous reactions
     + pyrolysis gas sources
   - Gas energy: enthalpy equation with reaction heat, gas-solid heat
     exchange, and radiation
   - Pressure correction: accounts for mass sources from pyrolysis
4. **Turbulence** update.

### Reaction types

- **Homogeneous** — gas-gas combustion (standard OpenFOAM reaction format
  in `constant/reactions`)
- **Heterogeneous** — gas-solid reactions with Arrhenius kinetics, four
  rate law variants, diffusion-limited option, and stoichiometric or
  non-stoichiometric mass partitioning

### Bed motion

The solid phase can advect (e.g. fuel bed settling). When porosity exceeds
a threshold (`criticalPorosity`), material from upstream cells is moved
downward — a simple bed-collapse model.

---

## Quick Start

```bash
# 1. Source OpenFOAM (adjust path for your installation)
source /path/to/OpenFOAM-v2406/etc/bashrc

# 2. Set porousGasificationFoam environment variables
cd /path/to/porousGasificationFoam
source porousGasificationMediaDirectories

# 3. Build everything
./Allwmake

# 4. Test by running a tutorial case
cd tutorials/cases/gasifier
./Allrun
```

This script runs `blockMesh`, `setFields`, `decomposePar`, and launches
`porousGasificationFoam -parallel` on 4 processes.

---

## Build System

### Simple build

```bash
# Build everything
./Allwmake

# Clean everything
./Allwclean
```

### Fine-grained control with build.sh

```bash
# Build only libraries
./build.sh build --libs-only

# Build only the solver and utilities
./build.sh build --apps-only

# Build specific targets
./build.sh build --radiationModels --pyrolysisModels --porousGasificationFoam

# Skip DEM (if YADE is not available)
./build.sh build --no-DEM

# Dry-run to preview commands
./build.sh build --all --dry-run

# Clean
./build.sh clean --all
```

### Build targets and dependencies

| Target | Type | Built by default | Dependency |
|---|---|---|---|
| DEM | library | yes (if WITH_YADE=1) | — |
| fieldPorosityModel | library | yes | — |
| radiationModels | library | yes | solid thermo |
| thermophysicalModels | library suite | yes | — |
| pyrolysisModels | library | yes | all above |
| porousGasificationFoam | executable | yes | all libraries |
| utilities | executable suite | yes | — |

Build order is automatic: libraries first, then applications.

### YADE DEM coupling

To enable the DEM module, build with:

```bash
WITH_YADE=1 ./build.sh build --all
```

You must have YADE installed with the OpenFOAM coupling module.
If YADE is not available, use `--no-DEM` or omit the flag (DEM is
not built by default without `WITH_YADE=1`).

---

## Case Structure

A typical case directory:

```
myCase/
├── 0/                          # Initial conditions
│   ├── p                       # Pressure [Pa]
│   ├── U                       # Velocity [m/s]
│   ├── T                       # Gas temperature [K]
│   ├── Ts                      # Solid temperature [K]
│   ├── rhos                    # Solid density [kg/m³]
│   ├── porosityF               # Porosity (0 = solid, 1 = gas)
│   ├── porosityF0              # Initial porosity (archived copy)
│   ├── Df                      # Darcy resistance tensor
│   ├── N2, O2, CO, ...         # Gas species mass fractions
│   ├── Ywood, Yash, ...        # Solid species mass fractions
│   ├── Ydefault                # Default field for unmatched gas species
│   └── YsDefault               # Default field for unmatched solid species
├── constant/
│   ├── thermophysicalProperties    # Gas-phase thermo (JANAF, Sutherland)
│   ├── thermo.compressibleGas      # Included gas species definitions
│   ├── solidThermophysicalProperties # Solid components and properties
│   ├── chemistryProperties         # Solid chemistry: reactions, solver settings
│   ├── reactions                   # Gas-phase homogeneous reactions
│   ├── pyrolysisProperties         # Pyrolysis model selection and parameters
│   ├── heatTransferProperties      # Gas-solid convective heat transfer
│   ├── specieTransferProperties    # Mass transfer (diffusion-limited reactions)
│   ├── radiationProperties         # Heterogeneous radiation model
│   ├── turbulenceProperties        # Laminar / RAS / LES
│   ├── g                           # Gravity vector
│   └── polyMesh/                   # Mesh (from blockMesh or other)
└── system/
    ├── controlDict                 # Time control, maxCo, maxDi
    ├── fvSchemes                   # Spatial and temporal discretization
    ├── fvSolution                  # Linear solvers, PIMPLE controls
    ├── blockMeshDict               # Mesh definition
    ├── setFieldsDict               # Initial field distribution
    ├── decomposeParDict            # Parallel decomposition
    └── probes                      # Point/line data sampling
```

---

## Input File Reference

### `constant/thermophysicalProperties`

Gas-phase thermodynamics. Standard OpenFOAM configuration:

```cpp
thermoType
{
    type            hePsiThermo;
    mixture         multiComponentMixture;
    transport       sutherland;
    thermo          janaf;
    energy          sensibleEnthalpy;
    equationOfState perfectGas;
    specie          specie;
}

inertSpecie N2;
solveEnergy true;

#include "thermo.compressibleGas";
```

The included file `thermo.compressibleGas` lists each gas species with
its JANAF `Cp(T)` coefficients, Sutherland transport coefficients, and
molecular weight.

---

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
    transport
    {
        K           0.341;   // Thermal conductivity [W/m/K]
    }
    thermodynamics
    {
        Cp          1800;    // Specific heat [J/kg/K]
        Hf          -1.04e6; // Heat of formation [J/kg]
    }
    density
    {
        rho         1050;    // Intrinsic solid density [kg/m³]
    }
};

ashCoeffs
{
    transport
    {
        K           0.15;
    }
    thermodynamics
    {
        Cp          2400;
        Hf          -12.38e6;
    }
    density
    {
        rho         650;
    }
};
```

Component dictionary names are formed as `<componentName>Coeffs`.

Three thermo variants are available:

| `solidThermoType` | Cp(T) | Transport | Notes |
|---|---|---|---|
| `constHeterogeneous` | constant | constant | Simplest, adequate for most cases |
| `expoHeterogeneous` | `c0·(T/Tref)^n0` | exponential | T-dependent Cp and K |
| `linearHeterogeneous` | constant | linear | T-dependent K = a·T + b |

---

### `constant/chemistryProperties`

Solid chemistry configuration — the most important file for defining
your reaction kinetics.

```cpp
chemistry           on;

solidChemistryType
{
    solver              solidOde;
    method              ODESolidHeterogeneousChemistryModel;
    solidThermoType     const<constRad<constThermo<constRho>>>;
}

initialChemicalTimeStep 1e-5;

solidReactionEnergyFromEnthalpy false;  // true = use Hf, false = use heatReact
stoichiometricReactions false;          // true = mass-conserving stoichiometry
diffusionLimitedReactions true;         // enable mass-transfer limitation
showRelativeReactionRates false;

solidOdeCoeffs
{
    solver      seulex;                 // ODE integrator
}

species  // Pyrolysis gas names (must match gas species)
(
    CO N2 O2
);

solidReactions
(
    // One or more reactions (see Reaction Format below)
);
```

#### Reaction Format

Each reaction entry has three lines:

```
irreversibleSolidArrheniusHeterogeneousReaction
wood + 1.25 O2 = 0.5 ash + 1.75 CO
(5.61e9 1.96e4 300 -6.22e6 1.0 1.0)
```

**Line 1 — Reaction type keyword:**

| Keyword | Rate formula `k(T) =` |
|---|---|
| `irreversibleSolidArrheniusHeterogeneousReaction` | `A·exp(-Ta/T)` (if `T ≥ Tcrit`, else 0) |
| `irreversibleSolidTemperatureArrheniusHeterogeneousReaction` | `A·T·exp(-Ta/T)` |
| `irreversibleSolidModArrHeterogeneousReaction` | `A·(T-Tcrit)^(2/3)·exp(-Ta/T)` |
| `irreversibleSolidConstHeterogeneousReaction` | `A` (constant) |

**Line 2 — Stoichiometric equation:**

Format: `[n]Reactant1 [+ n]Reactant2 = [n]Product1 [+ n]Product2`

- Reactants and products can be solid names (from `solidThermophysicalProperties/solidComponents`) or gas names (from `species` list above)
- Numeric stoichiometric coefficients are optional (default 1)
- Solid names and gas names are distinguished automatically

**Line 3 — Rate parameters:**

`(A Ta Tcrit heatOfReaction n1 n2 ...)`

| Parameter | Meaning | Units |
|---|---|---|
| `A` | Pre-exponential factor | varies |
| `Ta` | Activation temperature `Ea/R` | [K] |
| `Tcrit` | Minimum temperature for reaction | [K] |
| `heatOfReaction` | Heat released/absorbed (>0 = exothermic) | [J/kg] |
| `n1, n2, ...` | Reaction order for each LHS species in order | — |

---

### `constant/pyrolysisProperties`

```cpp
active          true;

heterogeneousPyrolysisModel  volPyrolysis;

pyrolysisCoeffs
{
    subintegrateHeatTransfer true;  // sub-cycle heat transfer
    bedCollapse             false;  // enable bed motion
    criticalPorosity        0.9999; // porosity threshold for bed collapse
    replenish               false;  // fuel replenishment model
}

infoOutput      true;
```

The only implemented pyrolysis model is `volPyrolysis`. It solves solid
species conservation, solid temperature, and porosity evolution.

---

### `constant/heatTransferProperties`

Gas-solid convective heat exchange coefficient.

```cpp
heatTransferModel constCONV;

Parameters
{
    h       8;      // Heat transfer coefficient [W/m²/K]
    SAV     468;    // Specific surface area [1/m]
}
```

The volumetric heat transfer rate is `h · SAV · (T_gas - T_solid)`.

---

### `constant/specieTransferProperties`

Mass transfer coefficient for diffusion-limited reactions.

```cpp
specieTransferModel constST;

Parameters
{
    h       0.017;  // Mass transfer coefficient
    SAV     468;    // Specific surface area [1/m]
}
```

Only used when `diffusionLimitedReactions true` in `chemistryProperties`.

---

### `constant/radiationProperties`

```cpp
radiation       on;

heterogeneousRadiationModel  heterogeneousP1;
solverFreq 1;

heterogeneousAbsorptionEmissionModel heterogeneousConstantAbsorptionEmission;

heterogeneousConstantAbsorptionEmissionCoeffs
{
    a               a  [ 0 -1  0 0 0 0 0 ] 0;      // Absorption coefficient [1/m]
    as              as [ 0 -1  0 0 0 0 0 ] 0;      // Scattering coefficient [1/m]
    borderAs        borderAs [ 0 -1  0 0 0 0 0 ] 180; // Surface absorption [1/m]
    E               E  [ 1 -1 -3 0 0 0 0 ] 0.0;     // Emissive power
    borderL         borderL  [ 0 1 0 0 0 0 0 ] 1.5e-3; // Surface layer thickness [m]
}
```

Two radiation models:
- `heterogeneousP1` — P1 approximation, accounts for solid-phase absorption
- `heterogeneousMeanTemp` — simplified mean-temperature model

Set to `none` to disable heterogeneous radiation.

---

### `constant/turbulenceProperties`

Standard OpenFOAM. Most porous-media cases use laminar flow:

```cpp
simulationType  laminar;
```

---

### `system/controlDict`

Key settings specific to this solver:

```cpp
application     porousGasificationFoam;

deltaT          0.001;
adjustTimeStep  yes;
maxCo           5;          // Courant number limit (fluid)
maxDi           5000;       // Diffusion number limit (solid)
```

`maxDi` controls the solid-phase time step through the diffusion
stability criterion computed by `solidRegionDiffusionNo.H`.

---

### `system/fvSolution`

Solvers for solver-specific fields:

```cpp
solvers
{
    p                   { solver GAMG; ... }
    "(U|h|Yi|Ys|porosity)"  { solver PBiCG; preconditioner DILU; ... }
    Ts                  { solver PCG; preconditioner DIC; tolerance 1e-10; }
    rhos                { solver PCG; preconditioner DIC; ... }
}

PIMPLE
{
    momentumPredictor   yes;
    nOuterCorrectors    2;
    nCorrectors         2;
    nNonOrthogonalCorrectors 0;
}
```

The `Ts` (solid temperature) solver needs a tight tolerance (1e-10)
for stability.

---

### `system/fvSchemes`

```cpp
ddtSchemes
{
    default         Euler;
}

divSchemes
{
    div(phiSolid)     Gauss upwind;
    div(phi,U)        Gauss linearUpwindV default;
    div(phi,Yi)       Gauss upwind;
    div(phi,h)        Gauss upwind;
}
```

`div(phiSolid)` must be `upwind` for stability of the solid advection equation.

---

## Required Initial Fields

| Field | Type | Dimensions | Description |
|---|---|---|---|
| `p` | volScalarField | `[1 -1 -2 0 0 0 0]` | Pressure [Pa] |
| `U` | volVectorField | `[0 1 -1 0 0 0 0]` | Velocity [m/s] |
| `T` | volScalarField | `[0 0 0 1 0 0 0]` | Gas temperature [K] |
| `Ts` | volScalarField | `[0 0 0 1 0 0 0]` | Solid temperature [K] |
| `rhos` | volScalarField | `[1 -3 0 0 0 0 0]` | Solid density [kg/m³] |
| `porosityF` | volScalarField | dimensionless | Porosity (0–1) |
| `porosityF0` | volScalarField | dimensionless | Archival initial porosity |
| `Df` | volTensorField | `[0 -2 0 0 0 0 0]` | Darcy resistance tensor |
| `<gasName>` (e.g. `O2`, `N2`, `CO`) | volScalarField | dimensionless | Gas species mass fraction |
| `Y<solidName>` (e.g. `Ywood`, `Yash`) | volScalarField | dimensionless | Solid species mass fraction |
| `Ydefault` | volScalarField | dimensionless | Default gas field (set to 0) |
| `YsDefault` | volScalarField | dimensionless | Default solid field (set to 0) |

**Notes:**

- `Df` is a tensor field. For an isotropic porous medium it is diagonal
  with large values in gas-only regions (e.g. `1e9`) and smaller values
  (based on permeability) in porous zones. Use the `setPorosity` utility
  to create appropriate fields.
- `porosityF0` stores the initial porosity distribution for reference.
  It is set to the same values as `porosityF` at time 0.
- Solid species field names are formed as `Y` + component name from
  `solidThermophysicalProperties` (e.g. component `wood` → field `Ywood`).
- Gas species field names match the `species` list in `chemistryProperties`.

---

## Tutorial Cases

All 13 cases under `tutorials/cases/`:

| Case | Description | Notable features |
|---|---|---|
| `microTGA/microTGAMeanTemp` | Micro-scale TGA with mean-temp radiation | Radiation on, P1 model |
| `microTGA/microTGAP1` | Micro-scale TGA with P1 radiation | Template case |
| `microTGA/mircoTGATemplate` | Micro TGA template (typo preserved) | Full configuration reference |
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

To run all cases for validation:

```bash
cd tutorials
./TestAllCases.sh
```

To clean all cases:

```bash
cd tutorials
./CleanAllCases.sh
```

---

## Utilities

### `setPorosity`

Creates `porosityF` and `Df` fields based on porous medium parameters
(particle diameter, tortuosity, permeability). Run before the simulation
to set up the porous resistance:

```bash
cd <caseDir>
setPorosity
```

Reads parameters from a dictionary; writes `0/porosityF` and `0/Df`.

### `totalMassPorousGasificationFoam`

Post-processing utility that integrates solid-state mass over the
computational domain at each stored time step. Useful for checking
mass conservation:

```bash
cd <caseDir>
totalMassPorousGasificationFoam
```

Outputs total solid mass per species vs. time.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `Cannot find library` | `porousGasificationMediaDirectories` not sourced | Run `source porousGasificationMediaDirectories` |
| `undefined symbol` | Library version mismatch or incomplete rebuild | Run `./build.sh clean --all && ./build.sh build --all` |
| Simulation crashes immediately | Missing initial fields | Check `constant/solidThermophysicalProperties` solidComponents match `0/Y*` files |
| Negative temperatures | Too large time step, or reaction parameters too aggressive | Reduce `deltaT`, `maxCo`, `maxDi`, or `initialChemicalTimeStep` |
| Porosity exceeds 1 or goes below 0 | Bed collapse active but too aggressive | Reduce `deltaT` or adjust `criticalPorosity` |
| Mass fractions do not sum to 1 | Missing `Ydefault` field or gas species | Check that all gas species have corresponding `0/` fields |
| Slow convergence in pressure | Tight PIMPLE settings or poor initial conditions | Increase `nCorrectors` or relax `tolerance` on `p` |
| Parallel: `decomposePar` fails | Missing decompose constraints | Ensure `Ts`, `porosityF`, `porosityF0` use `calculated` or `zeroGradient` BCs on processor boundaries |

---

## Documentation

Doxygen documentation can be generated:

```bash
cd doc/Doxygen
./Allwmake
```

Output: `$WM_PROJECT_DIR/doc/Doxygen/html/index.html`

Requires `doxygen` and `graphviz` packages:

```bash
sudo apt-get install doxygen graphviz
```
