# porousGasificationFoam

*porousGasificationFoam* (PGF) is an OpenFOAM solver for **thermochemical conversion in porous media**. A solid phase, represented as a porosity field, is coupled to a gas flowing through the void space. Mass, momentum, and energy are exchanged through the porosity. All chemistry — gas-phase, heterogeneous (gas–solid), and solid decomposition — is defined per case. With no reactions defined, both phases stay inert. The solid can be stationary or move under gravity and other external forces, shrink, or disappear entirely as conversion proceeds.

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

**Part II — Physics and Implementation**

11. [Two-Phase Coexistence](#two-phase-coexistence)
12. [Per-Time-Step Tour](#per-time-step-tour)
13. [Where to Look for What](#where-to-look-for-what)

[Development Workflow](#development-workflow) · [Citation](#citation) · [Contributors](#contributors)

---

## Part I — Practical Guide

*Build, run, configure, debug. Most readers spend their time here.*

## Quick Start

### Prerequisites

- OpenFOAM-v2406
- MPI (for parallel runs)
- Optional: YADE DEM library (for DEM coupling — build with `./Allwmake --yade`)

### Build and Install

```bash

# 1. Source OpenFOAM (adjust paths for your installation)
source /path/to/OpenFOAM-v2406/etc/bashrc

# 2. Build everything (in place — the checkout can live anywhere)
cd /path/to/porousGasificationFoam
./Allwmake

# 3. Test installation
cd tutorials/cases/charOnlyMoveCases/parallel
./Allrun
```

For a full reactive-flow demonstration see `gasifier`. The complete list is under [Tutorial Cases](#tutorial-cases). To run every case: `cd tutorials && ./TestAllCases.sh`. To clean a build: `./Allwclean`.

## Project Structure

```
porousGasificationFoam/
├── porousGasificationFoam/    # main solver source (time loop, equations)
├── porousGasificationMedia/   # libraries (pyrolysis, chemistry, radiation, DEM)
├── tutorials/cases/           # tutorial cases (input examples)
├── utilities/                 # auxiliary tools (setPorosity, totalMass, …)
├── applications/test/         # regression framework
├── Allwmake / Allwclean       # build / clean everything (non-DEM by default)
├── build.sh                   # fine-grained build script
└── doc/Doxygen/               # API documentation source
```

## Build System

### Where things are built and where they land

porousGasificationFoam uses the ordinary OpenFOAM user-build layout. `wmake`
runs **in place**, in the component directories of your checkout — the checkout
may live anywhere, and nothing is copied into `$WM_PROJECT_USER_DIR`.

| What | Where |
|---|---|
| Source | your checkout, wherever you cloned it |
| `lnInclude/`, `Make/<WM_OPTIONS>/` | beside the source, inside the checkout (git-ignored) |
| Libraries | `$FOAM_USER_LIBBIN` |
| Executables | `$FOAM_USER_APPBIN` |

`$FOAM_USER_LIBBIN` is already on the runtime library path once you have sourced
OpenFOAM's `etc/bashrc`, so no extra environment file has to be sourced before
building or running. (Earlier releases shipped a `porousGasificationMediaDirectories`
helper that mirrored the sources under `$WM_PROJECT_USER_DIR/applications` and
exported `FOAM_HGS`. Both the file and the mirror are gone; if you still have an
old mirror lying around, delete it — nothing consults it any more, and it can
only confuse a header search.)

`build.sh` resolves its own location, so `./Allwmake`, `./build.sh …` and
`/abs/path/to/checkout/Allwmake` all behave identically from any working
directory.

### Generated artifacts and cleaning

A build writes `lnInclude/` and `Make/<WM_OPTIONS>/` next to each component's
sources. Both are git-ignored; neither should ever be committed.

`./Allwclean` (or `./build.sh clean …`) removes that generated state through
`wclean`. Like `wclean`, it leaves the installed libraries and executables in
place: `$FOAM_USER_LIBBIN` and `$FOAM_USER_APPBIN` are shared by every project
built into this `$WM_PROJECT_USER_DIR`, so a clean in one checkout has no
business reaching into them.

When you do want a guaranteed blank slate — before switching build variants, or
after a rebuild failed and you would rather have no solver than the previous
one — add `--purge`:

```bash
./build.sh clean --all --purge
```

That additionally deletes the libraries and executables the cleaned targets
declare in their own `Make/files`, and nothing else. Other libraries in
`$FOAM_USER_LIBBIN`, including Foam-Yade's, are left alone.

One thing `wmake` will not do on its own is prune a symlink in `lnInclude/`
whose source file you deleted or renamed; it is left dangling, so an include of
it fails loudly rather than resolving to stale content. Run `./Allwclean` after
renaming or deleting headers to clear it.

### Build targets

| Target | Type | Dependency |
|---|---|---|
| DEM (`--yade` only) | library | Foam-Yade coupling libs |
| fieldPorosityModel | library | — |
| radiationModels | library | solid thermo |
| thermophysicalModels | library suite | — |
| pyrolysisModels | library | all above |
| porousGasificationFoam | executable | all libraries |
| utilities | executable suite | — |

### Fine-grained control with build.sh

The `build.sh` script operates in two modes: `build` or `clean`. For the selected mode it builds or cleans all active targets. Targets are switched on or off via CLI flags.

Examples:

```bash
./build.sh build --apps-only # set app targets to 1 and libs to 0, build targets with 1 set
./build.sh clean --reset-all --radiationModels # set all targets to 0; set radiationModels to 1, clean only radiationModels (later flags overwrite previous ones)
./build.sh build --all --no-DEM # skip DEM, build all the rest

# Aliases
./Allwmake  → ./build.sh build --all --no-DEM   # normal build: no DEM/Yade
./Allwclean → ./build.sh clean --all            # cleans DEM too
```

Add `--purge` to a clean to also delete the installed libraries and
executables; without it, cleaning matches `wclean` and touches build state only.

Both wrappers forward any extra arguments, so `./Allwmake --yade` is the
DEM-enabled build.

Run `build.sh --help` for all options.

### YADE DEM coupling

```bash
./build.sh build --yade
```

`--yade` enables the `DEM` library target and compiles the solver with `WITH_YADE=1`, which activates the `#ifdef WITH_YADE` DEM coupling blocks. Without this flag the solver is built without DEM support and Yade will hang waiting for MPI communication.

Requires YADE installed with the OpenFOAM coupling module, with `YADE_TRUNK`
pointing at the Foam-Yade source checkout. `build.sh` checks both before it
compiles anything, so a missing prerequisite is reported as an error instead of
a linker failure — and an existing normal build is left untouched.

**Switching between the normal and `--yade` builds requires a clean.** `wmake`
decides what to recompile from source timestamps; it does not know that
`WITH_YADE` changed, so an in-place rebuild after a mode switch would relink a
mix of the two variants. `build.sh` records which variant produced the objects
currently in the tree and refuses to build the other one on top of them:

```bash
./build.sh clean --all --purge   # then
./build.sh build --yade          # (or ./Allwmake for the normal build)
```

`--purge` is the right choice here specifically: it drops the installed solver
too, so a rebuild that fails partway cannot leave the *other* variant's binary
on your `PATH`.

Only one variant can exist in a given `$FOAM_USER_APPBIN` at a time; the two are
not built side by side.

### Doxygen documentation

```bash
cd doc/Doxygen
./Allwmake
```

Output: `$WM_PROJECT_DIR/doc/Doxygen/html/index.html`. Requires `doxygen` and `graphviz`.

The wrapper derives the source root from its own location, so it may also be run
by absolute path from anywhere; set `POROUS_DOC_SRC` explicitly only if you want
to document a different tree.

## Tutorial Cases

All 14 cases under `tutorials/cases/`:

| Case | Description | Notable features |
|---|---|---|
| `microTGA/` | Micro-scale TGA (3 variants: MeanTemp, P1, Template) | Radiation on, template reference |
| `macroTGA_688K/` | Macro-scale TGA at 688 K | — |
| `macroTGA_688K_fine/` | Macro-scale TGA at 688 K, refined mesh | — |
| `macroTGA_879K/` | Macro-scale TGA at 879 K | — |
| `macroTGA_879K_fine/` | Macro-scale TGA at 879 K, refined mesh | — |
| `macroTGA_experimentalData/` | TGA with experimental data comparison | — |
| `gasifier/` | Fixed-bed gasifier | - |
| `flatPlate/` | Flat plate reactive flow | — |
| `biomassPressureDrop/` | Pressure drop through biomass bed | — |
| `charOnlyMoveCases/parallel/` | Moving char (no reactions), 4-proc parallel | Solid material motion test |
| `charOnlyMoveCases/parallel-recon/` | Parallel + reconstructPar | Small fast parallel regression |
| `charOnlyMoveCases/serial/` | Serial cut-down of `parallel` | Regression fixture (prescribed-`Us` transport) |
| `charOnlyMoveCases/serial_m2/` | Serial, 2× mesh refinement | Convergence study |
| `charOnlyMoveCases/serial_m4/` | Serial, 4× mesh refinement | Convergence study |
| `MicroTGA-DEM/` | DEM-coupled micro TGA | Requires YADE |
| `simple_line_case/` | DEM-coupled 1-D packed line, pyrolysing | Requires YADE |

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
| `heatOfReaction` | Heat released/absorbed (<0 = exothermic) | [J/kg] |
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

### `constant/lambdaDict` (required by `WITH_YADE` builds running a DEM case)

Read only when DEM coupling is active (`constant/yadeProperties` with `active true`), by two independent consumers that share the one file: `volPyrolysis` (which model computes `lambdaDot`) and `lambdaDotModel` (how the DEM fields are smoothed onto the mesh).

```cpp
lambdaMode      exactDifferential;  // constant | exactDifferential

// -- lambdaMode constant
lambdaValue     0.0;         // [m/s]  fixed lambdaDot everywhere

// -- lambdaMode exactDifferential; every coefficient defaults to 0.0
dlambdaOverDTs             1e-6;    // [m/K]     dlambda/dTs
dlambdaOverDYmi            2.5e-12; // [m^4/kg]  dlambda/dYm_i, one value for
                                    //           every solid specie...
// dlambdaOverDYmi { char 2.5e-12; wood 1e-12; }  // ...or per specie, keyed by
                                    //           solidComponents name; species
                                    //           left out read 0.0
splitMassBetweenLamAndPor  0.5;     // [-] in [0,1]: share of the chemistry
                                    //     mass change taken as particle
                                    //     shrinkage; the rest becomes pore space

// -- Us interpolation (UsDEM -> Us), consumed by lambdaDotModel
interpolateUs              true;
interpolationMode          laplaceSetValues;  // laplaceAnchored | laplaceSetValues
solidPorosityCutoff        1;
nLaplaceSetValuesCorrectors 0;
demVelocityAnchorCoeff     1e6;     // laplaceAnchored only
backgroundUsAnchorCoeff    1e-12;   // laplaceAnchored only
nUsInterpolationCorrectors 1;       // laplaceAnchored only

// -- lambda interpolation (lambdaDEM -> lambda), same two strategies
interpolateLambda          true;
lambdaInterpolationMode    laplaceSetValues;
lambdaBackgroundValue      1.0;     // [m] value held outside the solid region
nLaplaceSetValuesLambdaCorrectors 0;
demLambdaAnchorCoeff       1e6;     // laplaceAnchored only
backgroundLambdaAnchorCoeff 1e-12;  // laplaceAnchored only
nLambdaInterpolationCorrectors 1;   // laplaceAnchored only
```

Leaving every `exactDifferential` coefficient at its `0.0` default gives `lambdaDot = 0` and sends the whole chemistry mass change to porosity — the behaviour before the feature existed, and the reason there is no separate "off" mode. For the governing equations and the dimension derivations, read the `Description` block at the top of `porousGasificationMedia/DEM/lambdaDotModels/exactDifferentialLambdaDot/exactDifferentialLambdaDot.H`.

The `Us` interpolation solve needs a `Us` entry under `system/fvSolution`'s `solvers` (and `UsDEMInterpolation` for `laplaceAnchored`). The lambda interpolation does **not**: it falls back to in-code controls (`smoothSolver`/`symGaussSeidel`, `tolerance 1e-6`, `relTol 0.01`) when no `lambda` / `lambdaDEMInterpolation` entry exists, and uses the case's entry when it does. It is an internal smoothing step, so an existing case does not have to gain an `fvSolution` key to keep running.

**Who owns what.** PGF owns the rate `lambdaDot`; YADE owns the integrated `lambda` (`State::lambda_`, advanced every DEM step in `NewtonIntegrator`) and sends it back through the particle-data buffer. PGF's `lambda` field is therefore derived output — nothing in the solver computes with it. The buffer stride is a compile-time constant on both sides with no MPI-level negotiation, so **Foam-Yade and PGF must be rebuilt together** whenever the coupling layout changes.

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

nly used when `diffusionLimitedReactions true`.

### `constant/porosityProperties` (optional)

Adds a Forchheimer (quadratic) resistance term to the momentum equation on top of the linear Darcy term. If the file is absent, `forchheimerCoeff` defaults to `0` and only the Darcy term `Df·U` is applied.

```cpp
forchheimerCoeff 3.21e6;
```

Effective momentum sink: `−µ_eff · D · ⟨u⟩  −  F_c · ρ_f · |⟨u⟩| · √3 · D / |D|`. The Forchheimer term matters for high-velocity flow through coarse beds. For slow flow inside a fine porous medium the linear Darcy term is usually sufficient. Unlike `Df` (a per-cell field), `forchheimerCoeff` is a single scalar applied everywhere.

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
div(phiSolid)     Gauss upwind;
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
3. **`setPorosity` utility** *(possibly outdated)*: a code-driven generator (see `utilities/setPorosity/`). The user-editable description of the medium lives in `medium.H` — the tool must be recompiled after each change. Kept for backward compatibility with older cases and parametric sweeps. For new cases, prefer approaches 1 and 2 above.

### Solid thermophysical properties: true density, not bulk

Entries in `solidThermophysicalProperties` (`rho`, `K`, `Cp`, `Hf`) describe the **pure solid** — i.e. the material at `porosityF = 0`. The macroscopic bulk density inside the bed is then `ρ · (1 − φ)`. PGF reconstructs it from `porosityF` and the intrinsic `rho`. Literature often reports bulk values instead, so be careful: feeding a bulk density into `rho` understates the solid by a factor of `(1 − φ)`.

### Gas species without JANAF data

Pyrolysis pseudo-species (e.g. lumped `tar`, `targas`) generally have no JANAF coefficients. In `thermo.compressibleGas` pick an existing species from `$FOAM_ETC/thermoData` whose molar mass and `Cp(T)` are the best available proxy for the pseudo-species. Document the substitution in the case to make the assumption auditable later.

### Radiation parameters are the hardest to source

The heterogeneous radiation coefficients (`a`, `as`, `borderAs`, `borderL`, `E`) rarely appear in literature with the right semantics. A workable approach is to tune them on a small TGA-like case so that the simulated heating rate matches an experiment, then reuse the tuned values across geometries that share the same surface chemistry and pellet morphology.

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

- **`setPorosity`** *(possibly outdated)* — generates `porosityF` and `Df` fields from medium parameters (particle diameter, tortuosity, permeability). Editable medium description in `utilities/setPorosity/medium.H`. Kept for backward compatibility — for new cases prefer the `setFields` + STL workflow (see [Tips](#biomass-distribution-initial-porosityf)).
- **`totalMassPorousGasificationFoam`** — post-processing diagnostic that writes `totalMass.txt` (time, integrated solid mass `∫ρ_s · (1−porosityF) dV`) for the run. Operates on a reconstructed case — run `reconstructPar` first if the case was decomposed.

## Regression Testing

Numerical regression tests live under `applications/test/regression/`. The framework runs selected tutorial cases, extracts a small set of summary scalars produced by OpenFOAM `volFieldValue` function objects, and diffs the results against a committed reference baseline within a numerical tolerance.

The point is to give refactors a safety net: any change that perturbs the selected scalars beyond tolerance fails the regression and surfaces a clear diff. The gate is deliberately **honest** — it never reports success unless it actually ran a case and compared it (see the outcome contract below).

The framework has two runners that share one outcome vocabulary:

- **Serial suite** — `Allrun` / `Allclean`, `cases.list`, `tools/runCase.sh`, `tools/compareScalars.py`.
- **DEM (Yade) suite** — `Allrun.yade`, `cases_yade.list`, `tools/runDEMCase.sh` (requires a Yade-enabled build; see below).
- **Shared logic** — `tools/regressionLib.sh` holds the pieces both runners must agree on: how a case list is loaded, how a case's exit status becomes an outcome, how the summary decides whether the suite is green, and the bounded-concurrency mechanics. `tools/regressionState.py` serialises run state as JSON (standard library only, no project imports).

Per-case bits live in `tutorials/cases/<caseName>/system/regressionFunctions` (`volFieldValue` blocks included from `controlDict`) and `tutorials/cases/<caseName>/reference/postProcessing/` (committed baseline).

**The suite scripts are self-contained.** They need bash, `python3` and the OpenFOAM environment, and nothing else — no external tool takes part in deciding which cases run, how they are cleaned, what a case's `Allrun` does, how output is compared, or whether the suite is green. Tooling that wants to watch a run reads the optional structured state described below; it is a reader, never a participant. If a helper ever appears to be *required* to run regressions, that is a bug in the helper.

### Outcome contract

Every case ends in exactly one outcome, keyed off the case's exit status (standard shell conventions), and both the printed summary and the suite exit code reflect it:

| Exit code | Outcome | Green? | Meaning |
|---|---|---|---|
| `0` | `PASS` | yes | ran, compared, within tolerance |
| `1` | `FAIL` | no | ran, compared, **diverged** beyond tolerance |
| `2` | `ERROR` | no | infrastructure: missing case dir, missing baseline, dirty slate, no `postProcessing/`, comparator missing |
| `124` | `TIMEOUT` | no | the run exceeded its timeout and its process group was terminated |
| `128+N` | `CRASH (SIG…)` | no | the run died on signal N (e.g. `134` → `CRASH (SIGABRT)`, `139` → `CRASH (SIGSEGV)`) |

**The only green suite is one where at least one case ran and every case PASSed.** There is no benign "skip": a case with no reference baseline is an `ERROR`, not a silently-passing skip; an empty or unreadable `cases.list` / `cases_yade.list` is an `ERROR`; a `--tag` or `--case` filter that matched no case is an `ERROR` (exit 2), not an empty green run. A suite that compared nothing can never report success. Skip a suite by not invoking its driver, never by emptying its inventory.

### Tiers

`cases.list` tags each case:

- **`fast`** — small fixtures, run by default (`./Allrun`) and intended as the pre-push gate.
- **`convergence`** — a mesh-refinement study; run manually with `--tag convergence` or `--tag all`.

The DEM cases are inventoried separately in `cases_yade.list` and run only by `Allrun.yade`.

### Hooking a case into regression

Two steps per case:

1. Drop a `system/regressionFunctions` file describing the metrics to extract (see `tutorials/cases/charOnlyMoveCases/serial/system/regressionFunctions` for the pilot example). Add `#includeIfPresent "regressionFunctions"` at the bottom of `system/controlDict` inside a `functions { ... }` block.
2. Add the case path (and optional tag) to `applications/test/regression/cases.list`.

Then capture a baseline (see below). Because a missing baseline is now an `ERROR`, a newly-registered case fails the suite until its baseline is committed — register and baseline together.

### Capturing the baseline

Run the case with the OpenFOAM toolchain available and commit the resulting `postProcessing/` under `reference/`:

```bash
cd tutorials/cases/charOnlyMoveCases/serial
./Allclean
./Allrun
mkdir -p reference
cp -r postProcessing reference/
git add reference
git commit -m "Capture charOnlyMoveCases/serial regression baseline"
```

After commit, every subsequent run of `applications/test/regression/Allrun` compares fresh output against `reference/postProcessing/` and reports the outcome.

### Running regressions

```bash
cd applications/test/regression
./Allrun                                  # fast tier (default), one case at a time
./Allrun --tag all                        # every case regardless of tag
./Allrun --case charOnlyMoveCases/serial  # a single case
./Allrun --no-run                         # compare existing output only
./Allrun --rtol 1e-3                      # override default relative tolerance
./Allrun --jobs 4                         # four cases at a time (see below)
./Allrun --list                           # show the selected cases, run nothing
./Allrun --state-dir /scratch/reg-state   # also write machine-readable run state
```

Exit code `0` means every case that ran PASSed; any non-zero means at least one case was not green (FAIL/ERROR/TIMEOUT/CRASH). The summary lists each case with its outcome and, on a FAIL, the rows that diverged; a CRASH shows the signal name so a teardown abort is not mistaken for a numerical divergence.

The solver must be on `PATH` (source the OpenFOAM environment and build the solver first). A run tier that cannot find the solver produces `ERROR`/`CRASH` outcomes, not a false green.

The per-case runners are supported entry points in their own right, for when you want one case without the suite around it:

```bash
tools/runCase.sh    tutorials/cases/canonical/pyrolysis [--no-run] [--rtol R] [--atol A]
tools/runDEMCase.sh tutorials/cases/simple_line_case [--timeout S] [--nsteps N]
```

Both return the same taxonomy as the suite, and both accept the optional state flags described below.

### Suite concurrency (`--jobs`) is not case decomposition

`--jobs N` is the number of regression **cases** the driver may have in flight at once. It says nothing about MPI: it does not become `-np N`, it does not touch `decomposeParDict`, and it does not change how any case invokes its solver.

Whether a case runs serially, calls `decomposePar`, uses `runParallel`, launches `mpirun` itself, or starts a Yade coupling process is part of the solution procedure being tested, and lives in that case's own `Allrun`. Nothing in the regression framework second-guesses it. Budget cores accordingly: `--jobs 4` over four 2-rank cases wants eight cores.

`--jobs 1` is exactly the default sequential run — same case order, same outcomes, same output. Concurrency changes only three things:

- each case's output is captured to a file instead of streaming live, replayed for failing cases after the run so the diagnostics stay readable and in `cases.list` order;
- a one-line verdict is printed as each case finishes;
- with no `--state-dir`, a temporary directory is created for that capture and removed on exit.

A malformed `--jobs` value (zero, negative, non-numeric) is an `ERROR`/exit 2 rather than a silently coerced default.

### Machine-readable listing and run state

Both drivers can describe a selection without touching it, and can record a run as it happens. Both are optional: with neither flag the drivers behave exactly as they always have and write nothing extra.

**Listing.** `--list` prints the selected cases; adding `--json` emits the manifest an external observer needs in order to know what a run of the *same arguments* will do — the filters are applied by one code path, so a listing cannot disagree with the run:

```bash
./Allrun --list --json --tag fast
./Allrun.yade --list --json
```

```json
{
  "schema_version": 1, "record": "listing",
  "suite": "regression", "driver": "Allrun",
  "project_root": "/path/to/pgf",
  "cases": [
    { "id": "tutorials/cases/canonical/pyrolysis", "name": "pyrolysis",
      "path": "/path/to/pgf/tutorials/cases/canonical/pyrolysis",
      "runner": "runCase.sh", "tag": "fast" }
  ]
}
```

A case `id` is its `cases.list`-relative path, not its basename, so two cases with the same basename can never be confused. (`cases.list` is whitespace-delimited, so a case path cannot contain spaces — a constraint of the inventory file, not of the protocol.) Listing a missing, empty or unmatched selection exits 2.

**Run state.** `--state-dir DIR` makes the driver record what it is doing, under a directory the caller owns — deliberately outside the cases, so a case's `Allclean` cannot delete the record of the run in progress:

```text
<state-dir>/
  suite.json            selection + options for this run
  events.ndjson         append-only event stream (one JSON object per line)
  result.json           final suite result, written atomically
  cases/<case-id>/
    state.json           current phase
    events.ndjson        this case's events
    result.json          this case's terminal result
    stdout.log, stderr.log
    compare.log          comparator output (serial suite)
```

Events are `suite_started`, `case_queued`, `case_started`, `case_phase`, `case_finished`, `suite_finished`; phases are `queued`, `clean`, `run`, `assert`, `compare`. Each terminal result carries the status, the **raw exit code**, the signal number and name for a signal death, timestamps and elapsed time, the phase it failed in, a human-readable message, artifact paths, and the comparator's file counts.

Two invariants make the state trustworthy:

- **The exit code remains the contract.** JSON is an additional representation of the same outcome, never a replacement, and a failed state write never changes a case's result.
- **An absent result is never a pass.** If a runner dies before writing one, the driver synthesises the outcome from the wait status it actually observed; if a case's own `result.json` disagrees with that status, the observed status wins and the disagreement is recorded (`result_mismatch`), so a stale file from an earlier run cannot turn a crash green.

`foamcli test` is one consumer of this contract — a convenience wrapper that runs `Allrun` and renders its state as a dashboard or JSON. It is optional in both directions: PGF never calls it, and it never decides whether a PGF case passed.

### DEM (Yade) smoke suite

```bash
applications/test/regression/Allrun.yade --short   # fast smoke: YADE_NSTEPS=200
applications/test/regression/Allrun.yade           # full run (hours)
applications/test/regression/Allrun.yade --list    # selected DEM cases, run nothing
```

Requires a Yade-enabled solver build (`./build.sh --yade` / `build-pgf --yade`), Yade, and `mpirun`. `--short` caps each case to a few hundred coupling steps so it runs in seconds and checks the coupling handshake and output rather than a converged result; a `YADE_NSTEPS`/`YADE_TIMEOUT` preset in the environment overrides the `--short` defaults. Each case is validated for `RUN FINISH`, an active DEM coupling handshake, sphere/spring VTK output, and a positive PGF time step in `dtInfo.txt`.

The DEM driver takes the same `--list`/`--json`/`--state-dir` flags as the serial one, and the same `--jobs N` — but it defaults to **one** case at a time and warns when you raise it, because concurrent DEM execution has not been verified: each case spawns its own `mpirun` → Yade → `MPI_Comm_spawn`'d solver tree, so two at once oversubscribe the machine in a way a serial case does not.

A hung DEM run is bounded by `YADE_TIMEOUT` and terminated by signalling **only its own process group** (`setsid` + `timeout --kill-after`) — the whole tree of `mpirun` → `yade` → the `MPI_Comm_spawn`'d solver. The runner never issues a host-wide `pkill`, which would kill unrelated solver runs elsewhere on the machine. `cases_yade.list` is the DEM inventory (kept separate from the serial `cases.list`).

### Known caveat: teardown abort surfaces as CRASH

Some cases run to completion and write correct output, then the solver aborts during teardown (a heap-corruption abort after `End`, e.g. `malloc_consolidate(): invalid chunk size`). Because the process did not exit cleanly, the hardened runner reports this as `CRASH (SIGABRT)`, **not** `PASS` — even though the case's scalars match the baseline within tolerance. This is deliberate: the runner reports what the process did, and a solver that corrupts its heap on the way out is not a healthy pass.

This is a solver/library teardown lifetime defect, tracked separately from the regression tooling. It is **intermittent**: the same binary running the same `fast`-tier case has aborted on every attempt in one environment and completed cleanly on every attempt in another, and the same fault has also been seen to surface as a teardown *hang* rather than an abort. So a green fast tier is evidence about a particular run, not about the code, and a red one is not necessarily a regression introduced by the change under test.

A tracked pre-push hook running the fast tier is therefore deferred until the teardown defect is fixed — a gate that goes red for reasons outside a given change would only train people to bypass it. Do **not** paper over the abort with `|| true`; that would trade an honest CRASH for a false green.

### What the comparison does

`tools/compareScalars.py` walks `reference/postProcessing/<funcName>/<time>/` files and, for each, locates the matching freshly-produced file under `postProcessing/<funcName>/<time>/`. It compares row-by-row, column-by-column with the rule:

```
pass if  abs(a - b) <= atol + rtol * max(|a|, |b|)
```

Defaults: `rtol=1e-4`, `atol=1e-12`. Both can be overridden per-run. The header line of OpenFOAM `volFieldValue.dat` files is parsed, so column names appear in failure reports.

### Limitations

- Comparison is over scalar reductions extracted by function objects, not full field comparisons. A regression that perfectly cancels in every selected scalar will go undetected. Add more metrics to catch a wider class of bugs.
- Floating-point sensitivity to MPI decomposition is real: rerun on the same `numberOfSubdomains` as the baseline. The pilot case `charOnlyMoveCases/serial` runs **serial (1 proc)** by design — it is a small, fast fixture for the prescribed-`Us` solid-transport path. (The full `charOnlyMoveCases/parallel` tutorial is large and its 4-proc run currently hits a processor-boundary breakdown in the moving-solid immersed boundary, so it is not used as a regression baseline.) Because the fixture is serial, this regression does not exercise the parallel path.
- The reference baseline is only as correct as the run that produced it. When you change the model intentionally, regenerate the baseline and state why in the commit message.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `Cannot find library` | OpenFOAM environment not sourced, or PGF not built into this `$WM_PROJECT_USER_DIR` | `source /path/to/OpenFOAM-v2406/etc/bashrc`, then `./Allwmake` |
| `Build variant mismatch` | Switching between the normal and `--yade` builds without cleaning | `./build.sh clean --all --purge`, then rebuild in the wanted mode |
| A deleted or renamed header still appears to be found | Dangling `lnInclude/` symlink from an earlier build | `./Allwclean && ./Allwmake` |
| `undefined symbol` | Library version mismatch or incomplete rebuild | `./build.sh clean --all && ./build.sh build --all` |
| Simulation crashes immediately | Missing initial fields | Check `solidComponents` match `0/Y*` files |
| Negative temperatures | Too large time step, or aggressive reaction parameters | Reduce `deltaT`, `maxCo`, `maxDi`, or `initialChemicalTimeStep` |
| Porosity exceeds 1 or goes below 0 | Bed collapse too aggressive | Reduce `deltaT` or adjust `criticalPorosity` |
| Mass fractions do not sum to 1 | Missing `Ydefault` field or gas species | Check all gas species have corresponding `0/` files |
| Slow convergence in pressure | Tight PIMPLE settings | Increase `nCorrectors` or relax `p` tolerance |
| Parallel: `decomposePar` fails | Missing decompose constraints | Ensure `Ts`, `porosityF`, `porosityF0` use `calculated` or `zeroGradient` BCs |

---

## Part II — Physics and Implementation

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
| Which input dictionary keys does X read? | Part I → [Input File Reference](#input-file-reference). |
| What initial fields does a case need? | Part I → [Required Initial Fields](#required-initial-fields). |
| Where does this field get constructed? | `porousGasificationFoam/createFields.H` for solver fields; `createDEMFields.H` for DEM-coupled fields. |
| How does the solver layout map to OpenFOAM modules? | Part I → [Project Structure](#project-structure). |

If you find a discrepancy between this tour and what the code does, the code wins — please open an issue or PR.

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

### Documentation

Source code is the source of truth for physics, equations, units, and algorithm choices. Comments live next to the implementation they describe — OpenFOAM-style file banner with a `Description` block, `//-` briefs above function declarations, and `// ...` narrative inside function bodies for non-obvious steps.

The README's [Part II](#part-ii--physics-and-implementation) is a tour: it narrates the per-step flow and points into the code. It does **not** restate equations or implementation detail — those belong in the source file that implements them, where they cannot drift unnoticed during refactors.

When you submit a PR:

- **Behaviour change** → update the relevant code-comment block in the same commit.
- **Loop structure or step ordering change** → update Part II's per-step tour too.
- **New documentation insight** → if it's about *what the code does*, write it as a code comment. If it's about *how to use* the solver or *how to navigate* the codebase, it belongs in the README.

If you catch a Part II claim that's already in the code as a comment, replace it with a pointer in the same PR. The migration is opportunistic — no need to wait for a dedicated cleanup branch.

---

## Citation

If you use this solver, please cite:

> Żuk, P. J., Tużnik, B., Rymarz, T., Kwiatkowski, K., Dudyński, M., Galeazzo, F. C., & Krieger Filho, G. C. (2022). OpenFOAM solver for thermal and chemical conversion in porous media. *Computer Physics Communications*, 278, 108407.

## Contributors

Paweł Jan Żuk, Bartosz Tużnik, Tadeusz Rymarz, Ali Ebrahimi Pure, Kamil Kwiatkowski, Marek Dudyński, Flavio C. C. Galeazzo, Guenther C. Krieger Filho, Filip Mróz (foam-extend-4.1 to v2406 port)

## For AI Coding Agents

This repository ships an `AGENTS.md` at the root with operating instructions for AI coding assistants (Claude Code, OpenCode, anything following the `AGENTS.md` / `CLAUDE.md` conventions). Open it if you collaborate with an agent on this codebase.
