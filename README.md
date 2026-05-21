# porousGasificationFoam

porousGasificationFoam (PGF) is an OpenFOAM solver for **thermochemical conversion in porous media**. A reactive solid phase, represented as a porosity field, is coupled to a compressible gas flowing through the void space; mass, momentum, and energy are exchanged through the porosity. The solid can be stationary or move under gravity and other external forces, shrink, or disappear entirely as conversion proceeds. Heterogeneous radiation, bed collapse, and DEM coupling for granular solids are optional.

The continuum transport equations are solved with the Finite Volume Method (OpenFOAM); an optional coupling to a Discrete Element Method solver via YADE handles the solid momentum equation when granular dynamics matter. Solid morphology, temperature, species, and reaction kinetics are user inputs — the solver is geometry- and chemistry-agnostic.

Reported and plausible application areas include:

- Biomass pyrolysis, gasification, and combustion (the solver's origin)
- Wood fires and waste incineration
- Coffee bean roasting and other slow thermal-conversion processes
- Peat smouldering
- Drying of moisture-laden porous beds
- Sintering and metal-foam manufacturing
- Dough rising and other expanding-bed processes

…and other processes that share a reactive or transforming porous solid exchanging mass and heat with a gas. The name reflects the solver's origins in biomass-to-syngas gasification (feedstock + air/O₂/steam/CO₂ → H₂/CO), not a limit on what it can model.

- **License**: GNU GPL v3
- **Target**: OpenFOAM-v2406
- **Optional**: DEM coupling via YADE (`WITH_YADE=1` at build time)
- Earlier ports for [OpenFOAM 8](https://github.com/btuznik/porousGasificationFoam) and foam-extend 4.1 exist but are no longer actively maintained.

## Contents

**Part I — Practical Guide**

1. [Quick Start](#quick-start)
2. [Project Structure](#project-structure)
3. [Build System](#build-system)
4. [Tutorial Cases](#tutorial-cases)
5. [Input File Reference](#input-file-reference)
6. [Required Initial Fields](#required-initial-fields)
7. [Tips for Preparing New Simulations](#tips-for-preparing-new-simulations)
8. [Utilities](#utilities)
9. [Regression Testing](#regression-testing)
10. [Troubleshooting](#troubleshooting)

**Part II — Physics and Code Structure**

11. [Two-Phase Coexistence](#two-phase-coexistence)
12. [Per-Time-Step Sequence](#per-time-step-sequence)
13. [Solid Chemistry — ODE Integration](#solid-chemistry--ode-integration)
14. [Porosity Evolution and Bed Motion](#porosity-evolution-and-bed-motion)
15. [Gas-Solid Coupling](#gas-solid-coupling)

[Development Workflow](#development-workflow) · [Citation](#citation) · [Contributors](#contributors)

---

## Part I — Practical Guide

*Build, run, configure, debug. Most readers spend their time here.*

## Quick Start

### Prerequisites

- OpenFOAM-v2406
- MPI (for parallel runs)
- Optional: YADE DEM library (for DEM coupling; set `WITH_YADE=1`)

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

`charOnlyMove` is the lightest tutorial (moving char bed, no chemistry) and works well as a first smoke test once the build succeeds:

```bash
cd tutorials/cases/charOnlyMove
./Allrun
```

For a full reactive-flow demonstration see `gasifier` (4-proc parallel biomass gasifier). The complete list is under [Tutorial Cases](#tutorial-cases). To run every case in turn: `cd tutorials && ./TestAllCases.sh`. To clean a build: `./Allwclean` (or `./build.sh clean --all`).

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

### Doxygen documentation

```bash
cd doc/Doxygen
./Allwmake
```

Output: `$WM_PROJECT_DIR/doc/Doxygen/html/index.html`. Requires `doxygen` and `graphviz`.

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
- Re-running a regression baseline must use the same `numberOfSubdomains` as the original; floating-point sensitivity to decomposition is real (see the "Regression Testing" section below).

## Utilities

- **`setPorosity`** — generates `porosityF` and `Df` fields from medium parameters (particle diameter, tortuosity, permeability). Run inside the case directory before the simulation; the editable medium description lives in `utilities/setPorosity/medium.H`.
- **`totalMassPorousGasificationFoam`** — post-processing diagnostic that writes `totalMass.txt` (time, integrated solid mass `∫ρ_s · (1−porosityF) dV`) for the run. Operates on a reconstructed case; run `reconstructPar` first if the case was decomposed.

## Regression Testing

Numerical regression tests live under `applications/test/regression/`. The framework runs selected tutorial cases, extracts a small set of summary scalars produced by OpenFOAM `volFieldValue` function objects, and diffs the results against a committed reference baseline within a numerical tolerance.

The point is to give refactors a safety net: any change that perturbs the selected scalars beyond tolerance fails the regression and surfaces a clear diff.

The framework lives under `applications/test/regression/` (top-level `Allrun`/`Allclean`, `cases.list`, `tools/runCase.sh`, `tools/compareScalars.py`). Per-case bits live in `tutorials/cases/<caseName>/system/regressionFunctions` (`volFieldValue` blocks included from `controlDict`) and `tutorials/cases/<caseName>/reference/postProcessing/` (committed baseline).

### Hooking a case into regression

Two steps per case:

1. Drop a `system/regressionFunctions` file describing the metrics to extract (see `tutorials/cases/charOnlyMove/system/regressionFunctions` for the pilot example). Add `#includeIfPresent "regressionFunctions"` at the bottom of `system/controlDict` inside a `functions { ... }` block.
2. Add the case path to `applications/test/regression/cases.list`.

Then capture a baseline (see below).

### Capturing the baseline

Run the case with the OpenFOAM toolchain available and commit the resulting `postProcessing/` under `reference/`:

```bash
cd tutorials/cases/charOnlyMove
./Allclean
./Allrun
mkdir -p reference
cp -r postProcessing reference/
git add reference
git commit -m "Capture charOnlyMove regression baseline"
```

After commit, every subsequent run of `applications/test/regression/Allrun` compares fresh output against `reference/postProcessing/` and reports PASS/FAIL.

### Running regressions

```bash
cd applications/test/regression
./Allrun                                # run every case in cases.list
./Allrun --case charOnlyMove            # run a single case
./Allrun --rtol 1e-3                    # override default relative tolerance
```

Exit code 0 means all cases within tolerance; non-zero means at least one case failed. The script prints a per-case PASS/FAIL summary and, on failure, the rows that diverged.

### What the comparison does

`tools/compareScalars.py` walks `reference/postProcessing/<funcName>/<time>/` files and, for each, locates the matching freshly-produced file under `postProcessing/<funcName>/<time>/`. It compares row-by-row, column-by-column with the rule:

```
pass if  abs(a - b) <= atol + rtol * max(|a|, |b|)
```

Defaults: `rtol=1e-4`, `atol=1e-12`. Both can be overridden per-run. The header line of OpenFOAM `volFieldValue.dat` files is parsed, so column names appear in failure reports.

### Limitations

- Comparison is over scalar reductions extracted by function objects, not full field comparisons. A regression that perfectly cancels in every selected scalar will go undetected. Add more metrics to catch a wider class of bugs.
- Floating-point sensitivity to MPI decomposition is real: rerun on the same `numberOfSubdomains` as the baseline. The pilot case `charOnlyMove` uses 4 procs.
- The reference baseline is only as correct as the run that produced it. When you change the model intentionally, regenerate the baseline and state why in the commit message.

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

---

## Part II — Physics and Code Structure

*How the solver works. Useful when porting cases, extending models, or interpreting unexpected results.*

## Two-Phase Coexistence

Gas and solid phases coexist in every cell, distinguished by the **porosity** field `porosityF`:

| `porosityF` | Meaning |
|---|---|
| 1.0 | Pure gas (no solid) |
| 0.0 | Pure solid (fully dense) |
| 0 < `porosityF` < 1 | Porous medium containing both phases |

The solid mass per unit volume is `rhos = rho * (1 - porosityF)` [kg/m³], where `rho` is the intrinsic solid material density. Mass, momentum, and energy are exchanged between phases through coupling source terms computed by the pyrolysis/chemistry model.

## Per-Time-Step Sequence

The main solver loop in `porousGasificationFoam.C` executes the following sequence each time step:

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

### Step 1: Time-Step Control

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

### Step 2: Radiation

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

### Step 3: Solid Phase Evolution (`pyrolysisZone.evolve()`)

This is the heart of the solver, implemented in `volPyrolysis::evolveRegion()` (`volPyrolysis.C:1214`). It performs five sub-steps:

#### 3a. Solid Chemistry ODE Integration

`solidChemistry_->solve(t0, deltaT)` integrates per-cell ODEs for solid species conversion, gas generation, and temperature change, and returns the characteristic chemical timescale. See [Solid Chemistry — ODE Integration](#solid-chemistry--ode-integration) below for details.

#### 3b. Chemical Energy Source

`chemistrySh_ = solidChemistry_->Sh()()` computes heterogeneous reaction heat [W/m³]. Two modes:
- `solidReactionEnergyFromEnthalpy = true`: uses heats of formation `hf` of solid and gas species
- `solidReactionEnergyFromEnthalpy = false`: uses specified `heatOfReaction` from reaction definition

#### 3c. Energy to Heat Pyrolysis Gases

`heatUpGas_ = heatUpGasCalc()()` heats (or cools) pyrolysis gases from solid temperature `Ts` to gas temperature `Tg`. Magnitude: `Sρ · Cp_gas · (Ts − Tg)`.

#### 3d. Solid Species Mass Conservation (`solveSpeciesMass()`)

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

#### 3e. Porosity Evolution (`evolvePorosity()`)

```
∂φ/∂t = RRpor - ∇·(Us · φ)
```
where:
```
RRpor = -Σ(ω_s,i / ρ_s,i)    [1/s]
```

After solving, cells where porosity exceeds `criticalPorosity` (default 0.9999) are identified for the bed-collapse algorithm. If `bedCollapse` is enabled, material from downstream cells is shifted upward to replenish solid mass.

#### 3f. Solid Energy Equation (`solveEnergy()`)

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

## Solid Chemistry — ODE Integration

The chemistry model is `ODESolidHeterogeneousChemistryModel` in `thermophysicalModels/porousSolidChemistryModel/`.

### Reaction Rate Laws

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

### Diffusion-Limited Reactions

When `diffusionLimitedReactions = true`, the effective rate is limited by mass transfer:

```
1/k_eff = 1/k_kinetic + Σ(1 / (ST · ρ_g · Y_gas_reactant))
```

where `ST` is the mass transfer coefficient [1/s] from `constant/specieTransferProperties`.

### Per-Cell ODE System

For each cell containing solid, the following system is solved:

```
d(ρ_s · Y_s,i)/dt = ω_s,i              (i = 1..nSolids)
d(ρ_g · Y_g,j)/dt = ω_g,j              (j = 1..nGases)
dT/dt = -Σ(ω_i · H_i) / Σ(Y_i · Cp_i)   (temperature)
```

The last equation is derived from the energy balance: reaction heat changes temperature via `Cp·dT/dt = -Σ(ω_i · hf_i)` when using enthalpy-based energy, or `Cp·dT/dt = heatOfReaction` when using specified heats.

### ODE Sub-Cycling Algorithm (`calculateSourceTerms()`)

Adaptive sub-cycling: each sub-step calls the ODE solver, returns the characteristic chemical timescale `tauC_`, advances temperature from the reaction enthalpy, then shrinks the next `dt_` to `min(timeLeft, tauC_)`. The default solver is `seulex` (implicit extrapolation), configured in `chemistryProperties`.

### Mass Partitioning

Two modes controlled by `stoichiometricReactions`:

| Mode | Description |
|---|---|
| `false` (default) | Mass fractions split by stoichiometric coefficient ratios. Total substrates mass = total products mass. |
| `true` | Uses molecular weights to compute mass-conserving partitioning. Accounts for differences in molar masses between reactants and products. |

In both modes, mass is strictly conserved. If a solid substrate converts to both solid products and gas products, the mass ratio between solid and gas products is proportional to the stoichiometric coefficients.

## Porosity Evolution and Bed Motion

### Porosity Source

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

### Bed Collapse Model

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

## Gas-Solid Coupling

Each conserved quantity has equal-and-opposite source terms in the solid and gas equations.

| Quantity | Solid side | Gas side |
|---|---|---|
| Mass | `−ω_s` (loss) | `+ω_s` in continuity, species, pressure equations |
| Species `i` | `−ω_s,i` | `+ω_s,i` in `YEqn` for the gas species |
| Energy | `+Sh_chem − Q_conv − Q_heatUpGas + Q_rad` in `solveEnergy()` | `+Q_conv + Q_heatUpGas + radiation->Sh()` in `EEqn` |
| Momentum | (no solid-side source) | `−Df·U` Darcy (+ optional Forchheimer) in `UEqn` |
| Volume | occupies `(1 − porosityF)` | `porosityF` multiplies gas-phase volume terms |

Term definitions:

- `Sρ_total = Σ ω_s,i` [kg/m³/s] — total mass transferred solid→gas; in gas equations it is limited to `(1 − porosityF)`.
- `Sρ(i) = ω_g,i` [kg/m³/s] — mass of gas species `i` produced by pyrolysis.
- `Q_conv = h · SAV · (Ts − Tg)` [W/m³] — convective gas-solid heat exchange.
- `Q_heatUpGas = Sρ · Cp_gas · (Ts − Tg)` [W/m³] — energy needed to heat pyrolysis products from `Ts` to `Tg`.
- `Sh_chem` (`chemistrySh_`) [W/m³] — heterogeneous reaction heat.
- `radiation->Sh()` [W/m³] — radiative source term, sign depending on local emission/absorption balance.

---

## Development Workflow

*Contributing to this repository. New contributors: start here.*

### Branch Naming

Branches use a short prefix that signals intent, followed by a kebab-case descriptive name. Five prefixes cover everything:

| Prefix | Used for |
|---|---|
| `feature/` | New user-facing capability — a new solver field, model, or tutorial exercising real functionality. |
| `fix/` | Observable bug becomes right. |
| `refactor/` | Code restructure intended not to change behaviour. Flag for careful review. |
| `docs/` | Documentation only (README, AGENTS.md, comments). |
| `chore/` | Everything else meta — formatter configs, build/Make tweaks, `.gitignore`, dependency bumps, tooling. |

Examples: `feature/UsInterp-laplace-smoothing`, `fix/regression-allrun-set-u`, `chore/format-and-docs`.

A single branch may bundle multiple low-risk meta concerns (e.g. formatter, docs, and dev-workflow changes can ride on one `chore/...` branch). Anything that can affect numerical results stays on its own branch.

### Merge Strategy

The repository uses **squash merge** — each PR becomes one commit on `main`, whose message is the PR title followed by the PR body. This keeps `main` linear and easy to scan with `git log --oneline`, while preserving the *why* and verification context inside `git log` / `git show`.

Because the squash commit is permanent and per-branch commits are not, the convention below applies to **PR titles and bodies**, not to individual commits on your working branch. Commit however you like locally; the squash collapses it.

### PR Titles

```
<type>[(<scope>)]: <imperative subject>
```

- `<type>` is one of `feat`, `fix`, `refactor`, `docs`, `chore` — the same vocabulary as branch prefixes (`feat` is the short form of `feature`).
- `<scope>` is optional. Use it when it adds clarity (e.g. `fix(regression): ...` vs an unrelated bug fix). Plausible scopes in this repo: `regression`, `DEM`, `solver`, `tutorials`, `README`, `AGENTS`, `pyrolysis`, `chemistry`.
- Subject is in imperative mood, lowercase first letter after the colon, ≤72 characters, no trailing period.

Examples:

```
feat(DEM): add UsInterp Laplace smoothing for solid velocity
fix(regression): keep Allrun/Allclean working under set -u
docs(README): clarify default chemistry
chore: apply clang-format across solver
```

### PR Body

Opening a PR auto-populates the description from `.github/pull_request_template.md`. Three sections:

- **Summary** — one or two sentences on what the PR changes. Always fill this in.
- **Why** — motivation: symptom, missing capability, or bug. Skip if the diff is obviously self-justifying (typo fix, formatter run).
- **Verification** — what you actually ran or checked: build target, tutorial cases, regression suite, residual sanity. Skip if the change cannot affect runtime (README only).

Aim for terse and specific. The body becomes part of `main`'s history once squash-merged, so future-you (and `git log`) benefit from precision now.

---

## Citation

If you use this solver, please cite:

> Żuk, P. J., Tużnik, B., Rymarz, T., Kwiatkowski, K., Dudyński, M., Galeazzo, F. C., & Krieger Filho, G. C. (2022). OpenFOAM solver for thermal and chemical conversion in porous media. *Computer Physics Communications*, 278, 108407.

## Contributors

Paweł Jan Żuk, Bartosz Tużnik, Tadeusz Rymarz, Zhiwar, Kamil Kwiatkowski, Marek Dudyński, Flavio C. C. Galeazzo, Guenther C. Krieger Filho, Filip Mróz (foam-extend-4.1 to v2406 port)

## For AI Coding Agents

This repository ships an `AGENTS.md` at the root with operating instructions for AI coding assistants (Claude Code, OpenCode, anything following the `AGENTS.md` / `CLAUDE.md` conventions). Open it if you collaborate with an agent on this codebase.
