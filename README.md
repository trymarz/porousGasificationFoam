# porousGasificationFoam

*porousGasificationFoam* (PGF) is an OpenFOAM solver for **thermochemical conversion in porous media**. A solid phase, represented as a porosity field, is coupled to a gas flowing through the void space. Mass, momentum, and energy are exchanged through the porosity. All chemistry — gas-phase, heterogeneous (gas–solid), and solid decomposition — is defined per case — with no reactions defined, both phases stay inert. The solid can be stationary or move under gravity and other external forces, shrink, or disappear entirely as conversion proceeds.

The continuum transport equations are solved with the Finite Volume Method (OpenFOAM). An optional coupling to a Discrete Element Method solver YADE provides PGF with a velocity field for the solid momentum equation. Solid material properties, initial distribution of porosity throughout a domain, reaction kinetics, and many other parameters — all these are user inputs — the solver is geometry- and chemistry-agnostic.

Example application areas:

- Biomass pyrolysis, gasification, and combustion (the solver's origin)
- Wood fires and waste incineration
- Coffee bean roasting
- Peat smouldering
- Metal-foam manufacturing

…and other processes that share a reactive or transforming porous solid exchanging mass and heat with a gas. The name *porousGasificationFoam* reflects the solver's origins in biomass-to-syngas gasification (feedstock + air/O₂/steam/CO₂ → H₂/CO), not a limit on what it can model.

- **License**: GNU GPL v3
- **Target**: OpenFOAM-v2406
- **Optional**: DEM coupling via YADE (`WITH_YADE=1` at build time)
- Earlier versions for [OpenFOAM 8](https://github.com/btuznik/porousGasificationFoam) and foam-extend 4.1 exist but are no longer actively maintained.

## Contents

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

[Citation](#citation) · [Contributors](#contributors)

## Quick Start

### Prerequisites

- OpenFOAM-v2406
- MPI (for parallel runs)
- Optional: YADE DEM library (for DEM coupling — set `WITH_YADE=1`)

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

Effective momentum sink: `−µ_eff · D · ⟨u⟩  −  F_c · ρ_f · |⟨u⟩| · √3 · D / |D|`. The Forchheimer term matters for high-velocity flow through coarse beds (≳ 1 m/s). For slow flow inside a fine porous medium the linear Darcy term is usually sufficient. Unlike `Df` (a per-cell field), `forchheimerCoeff` is a single scalar applied everywhere.

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

1. **`setFields`** (with or without `setSet`): the standard OpenFOAM workflow. Specify a `setFieldsDict` that sets `porosityF` inside a cell zone to the desired void fraction and leaves the rest at `1.0`. `setFields` alone is sufficient for simple geometries. Pair with `setSet` (batch mode) for more involved selection logic. Other fields (`T`, `Ts`, solid species mass fractions, …) can be initialised the same way. See `tutorials/cases/macroTGA_*` for an example chain (`blockMesh` → `setSet` → `refineHexMesh` → `setFields`).
2. **STL + `setFields`**: for non-trivial bed geometries, build an STL of the porous region in Salome or Blender and feed it to `setSet`/`setFields` via a `surfaceToCell` selector.
3. **`setPorosity` utility**: a code-driven generator (see `utilities/setPorosity/`). The user-editable description of the medium lives in `medium.H` — the tool must be recompiled after each change. Recommended only when scripted parametric sweeps are needed.

### Solid thermophysical properties: true density, not bulk

Entries in `solidThermophysicalProperties` (`rho`, `K`, `Cp`, `Hf`) describe the **pure solid** — i.e. the material at `porosityF = 0`. The macroscopic bulk density inside the bed is then `ρ · (1 − φ)`. PGF reconstructs it from `porosityF` and the intrinsic `rho`. Literature often reports bulk values instead, so be careful: feeding a bulk density into `rho` understates the solid by a factor of `(1 − φ)`. The exception is the `gasifier` tutorial, which deliberately models the entire packed bed at the megascopic scale using bulk wood density (`rho 663` kg/m³ vs ≈ 1050 kg/m³ for true wood). This is a modelling choice consistent with treating the bed as a homogeneous Darcy medium.

### Gas species without JANAF data

Pyrolysis pseudo-species (e.g. lumped `tar`, `targas`) generally have no JANAF coefficients. In `thermo.compressibleGas` pick an existing species from `$FOAM_ETC/thermoData` whose molar mass and `Cp(T)` are the best available proxy for the pseudo-species. Document the substitution in the case to make the assumption auditable later.

### Radiation parameters are the hardest to source

The heterogeneous radiation coefficients (`a`, `as`, `borderAs`, `borderL`, `E`) rarely appear in literature with the right semantics. The recommended workflow is to tune them on a small TGA-like case so that the simulated heating rate matches an experiment, then reuse the tuned values across geometries that share the same surface chemistry and pellet morphology. The macroTGA tutorials are sized for exactly this calibration step.

### Time-step coupling between gas and solid chemistry

Gas-phase reactions are typically orders of magnitude faster than heterogeneous solid reactions. When both are active, gas chemistry dominates the chemistry-limited time step and can make slow processes (e.g. gasification of a whole bed) very expensive. Practical implications:

- For slow heterogeneous processes, prefer chemistry mechanisms with the minimum gas-phase detail needed for the result you care about.
- `maxCo`, `maxDi`, and `initialChemicalTimeStep` in `controlDict` / `chemistryProperties` interact — tightening any one of them can mask the actual bottleneck. Check the solver log for which limit is binding before tuning.

### Mesh and parallel execution

- Run `setPorosity` (if used) **before** `decomposePar` — it operates on the reconstructed mesh.
- The `totalMassPorousGasificationFoam` utility also requires a reconstructed case. Run `reconstructPar` first, or operate on the un-decomposed run.
- `paraView -builtin` can visualise the decomposed processor directories directly without reconstruction — useful for very large cases.
- Re-running a regression baseline must use the same `numberOfSubdomains` as the original — floating-point sensitivity to decomposition is real (see the "Regression Testing" section below).

## Utilities

- **`setPorosity`** — generates `porosityF` and `Df` fields from medium parameters (particle diameter, tortuosity, permeability). Run inside the case directory before the simulation. The editable medium description lives in `utilities/setPorosity/medium.H`.
- **`totalMassPorousGasificationFoam`** — post-processing diagnostic that writes `totalMass.txt` (time, integrated solid mass `∫ρ_s · (1−porosityF) dV`) for the run. Operates on a reconstructed case — run `reconstructPar` first if the case was decomposed.

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

Exit code 0 means all cases within tolerance — non-zero means at least one case failed. The script prints a per-case PASS/FAIL summary and, on failure, the rows that diverged.

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

## Citation

If you use this solver, please cite:

> Żuk, P. J., Tużnik, B., Rymarz, T., Kwiatkowski, K., Dudyński, M., Galeazzo, F. C., & Krieger Filho, G. C. (2022). OpenFOAM solver for thermal and chemical conversion in porous media. *Computer Physics Communications*, 278, 108407.

## Contributors

Paweł Jan Żuk, Bartosz Tużnik, Tadeusz Rymarz, Zhiwar, Kamil Kwiatkowski, Marek Dudyński, Flavio C. C. Galeazzo, Guenther C. Krieger Filho, Filip Mróz (foam-extend-4.1 to v2406 port)

## For AI Coding Agents

This repository ships an `AGENTS.md` at the root with operating instructions for AI coding assistants (Claude Code, OpenCode, anything following the `AGENTS.md` / `CLAUDE.md` conventions). Open it if you collaborate with an agent on this codebase.
