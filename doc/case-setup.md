# Setting up your own case

A PGF case is a normal OpenFOAM case plus extra files for the solid and its chemistry. This page covers only those.

## Your first case

Start from `tutorials/cases/canonical/pyrolysis`: one solid (`wood`), one reaction (`wood = targas`), a 1-D mesh, about a second to run.

1. **Copy it:** `cp -r tutorials/cases/canonical/pyrolysis myCase`. `./Allrun` restores `0/` from `0.orig/`, runs `blockMesh`, then the solver. `reference/` and `system/regressionFunctions` are for the regression tests and can go.
2. **Mesh and fields:** edit `system/blockMeshDict` and `0.orig/` ([required fields](#required-initial-fields)). Values here are uniform; for a partial bed use `setFields` ([how](#biomass-distribution-initial-porosityf)).
3. **Solid:** list components in `constant/solidThermophysicalProperties`, with true (not bulk) density ([why](#solid-thermophysical-properties-true-density-not-bulk)). Each needs a `Y<name>` field.
4. **Chemistry:** set gas `species` and `solidReactions` in `constant/chemistryProperties` ([reference](#constantchemistryproperties)). No reactions means both phases stay inert.
5. **Gas species:** each needs a field in `0.orig/` and an entry in `constant/thermo.compressibleGas` ([no JANAF data?](#gas-species-without-janaf-data)).
6. **Run:** `./Allrun`, then check `log.porousGasificationFoam`. If it is slow, see [time-step coupling](#time-step-coupling-between-gas-and-solid-chemistry).

Other tutorials: [Tutorial Cases](#tutorial-cases). File details: [Input File Reference](#input-file-reference).

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

## Tutorial Cases

Cases under `tutorials/cases/`. Run all of them with `cd tutorials && ./TestAllCases.sh`.

| Case | What it is | Notes |
|---|---|---|
| `microTGA/` | Micro-scale TGA (MeanTemp, P1, Template variants) | Radiation on |
| `macroTGA_688K/`, `macroTGA_879K/` | Macro-scale TGA at 688 K / 879 K | `*_fine/` = refined mesh |
| `macroTGA_experimentalData/` | TGA with experimental data comparison | |
| `gasifier/` | Fixed-bed gasifier | Full reactive-flow demo |
| `flatPlate/` | Flat plate reactive flow | |
| `biomassPressureDrop/` | Pressure drop through a biomass bed | |
| `canonical/*` | Small single-feature cases (pyrolysis, gasification, radiation, enthalpy, …) | Regression fixtures |
| `charOnlyMoveCases/` | Moving char, no reactions | `serial` is the small fast one; `parallel` is 4-proc |
| `prescribedUsCases/*` | Prescribed solid velocity: converging, diverging, shear, vortex, … | Solid-transport tests |
| `solidAdvection/` | Solid advection | |
| `MicroTGA-DEM_*` | DEM-coupled micro TGA, one per `lambdaDot` model | Require a `--yade` build |
| `simple_line_case/` | DEM-coupled 1-D packed line, pyrolysing | Requires a `--yade` build |
| `case_line_interpolation/` | Line interpolation test geometry | |

`MicroTGA-DEM_lambdaDot_common/` is shared input for the DEM fixtures, not a runnable case.

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
    density         { rho         1050; rhoApparent 630; }   // [kg/m³] each
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
lambdaMode      exactDifferential;  // constant | exactDifferential | apparentVolume

// -- lambdaMode constant
lambdaValue     0.0;         // [m/s]  fixed lambdaDot everywhere

// -- lambdaMode exactDifferential; both coefficients default to 0.0 and may
//    take either sign
dlambdaOverDTs             1e-6;    // [m/K]     dlambda/dTs
dlambdaOverDYmi            2.5e-6;  // [m^4/kg]  dlambda/dYm_i, one value for
                                    //           every solid specie...
// dlambdaOverDYmi { char 2.5e-6; wood 1e-6; }  // ...or per specie, keyed by
                                    //           solidComponents name; species
                                    //           left out read 0.0

// -- lambdaMode apparentVolume: lambdaDot is the dimensionless factor r that
//    YADE multiplies lambda by once per exchange (1 = no change); uses each
//    specie's rhoApparent, all species count as structural
minSolidVolume             1e-6;    // [-] V_old below this: r = 1 (optional)
minVolumeRatio             1e-6;    // [-] floor on 1 + dV/V_old (optional)

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

Leaving every `exactDifferential` coefficient at its `0.0` default gives `lambdaDot = 0` — the reason there is no separate "off" mode. Porosity is unaffected either way: it is by definition `1 - Vsolid/Vfvm`, so its source always takes the full chemistry mass-loss rate, independently of what `lambdaDot` does. For the governing equations and the dimension derivations, read the `Description` block at the top of `porousGasificationMedia/DEM/lambdaDotModels/exactDifferentialLambdaDot/exactDifferentialLambdaDot.H`.

A physically defensible starting `dlambdaOverDYmi` for a real case is nearer `1e-3` [m⁴/kg] — the order of magnitude for a particle losing roughly a third of its length over full conversion of ~140 kg/m³ of solid. That is **not** what the values shown above or in the regression fixtures (`applications/test/regression/cases_yade.list`) use: those are tuned to keep both `exactDifferential` terms visible at the fixtures' short-run scale, ~400× smaller than this estimate, and a coefficient near `1e-3` would swamp the temperature term at that scale and flip the sign the `...Both` fixture asserts.

Both interpolation solves look up their linear-solver controls under `system/fvSolution`'s `solvers` by the name of the field being smoothed — `Us` and `lambda` — and fall back to in-code controls (`smoothSolver`/`symGaussSeidel`, `tolerance 1e-6`, `relTol 0.01`) when the entry is absent. The separate name `UsDEMInterpolation` is not consulted; the committed cases match both names with one regex entry. These are internal smoothing steps, so an existing case does not have to gain an `fvSolution` key to keep running.

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

Only used when `diffusionLimitedReactions true`.

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

## Tips for Preparing New Simulations

Practical notes that are easy to get wrong on a first PGF case.

### Biomass distribution (initial `porosityF`)

Three approaches, in increasing order of complexity:

1. **`setFields`** (with or without `setSet`): the standard OpenFOAM workflow. Specify a `setFieldsDict` that sets `porosityF` inside a cell zone to the desired void fraction and leaves the rest at `1.0`. `setFields` alone is sufficient for simple geometries. Pair with `setSet` (batch mode) for more involved selection logic. Other fields (`T`, `Ts`, solid species mass fractions, …) can be initialised the same way. See `tutorials/cases/macroTGA_*` for an example chain (`blockMesh` → `setSet` → `refineHexMesh` → `setFields`).
2. **STL + `setFields`**: for non-trivial bed geometries, build an STL of the porous region in Salome or Blender and feed it to `setSet`/`setFields` via a `surfaceToCell` selector.
3. **`setPorosity` utility** *(possibly outdated)*: a code-driven generator (see `utilities/setPorosity/`). The user-editable description of the medium lives in `medium.H` — the tool must be recompiled after each change. Kept for backward compatibility with older cases and parametric sweeps. For new cases, prefer approaches 1 and 2 above.

### Solid thermophysical properties: true density, not bulk

Entries in `solidThermophysicalProperties` (`rho`, `K`, `Cp`, `Hf`) describe the **pure solid** — i.e. the material at `porosityF = 0`. The macroscopic bulk density inside the bed is then `ρ · (1 − φ)`. PGF reconstructs it from `porosityF` and the intrinsic `rho`. Literature often reports bulk values instead, so be careful: feeding a bulk density into `rho` understates the solid by a factor of `(1 − φ)`.

`rhoApparent` is a third, separate quantity and is **required** for every solid specie: the apparent (envelope) density of **one particle**, i.e. its mass over its own volume including internal pores. It lies below the true `rho`, and it is not the bulk density of a packed bed, which also counts the gaps between particles. Only the ratios between species matter to the particle-size update (`lambdaMode apparentVolume`), so a consistent set matters more than the absolute values. A missing or non-positive entry stops the run at startup, naming the file and specie.

Material data lives in one database, `thermoData/solidThermoData`, one entry per material with its source; the file header states the basis of each value. A case includes it and maps its reaction names onto entries, e.g. `#include "../../../../thermoData/solidThermoData"` (path relative to the including file) and then `woodCoeffs { $park2010Wood; }`. Numerical fixtures may keep local values instead; there `rhoApparent` equals `rho`.

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
- Re-running a regression baseline must use the same `numberOfSubdomains` as the original — floating-point sensitivity to decomposition is real (see [Regression testing](regression-testing.md)).

## Utilities

- **`setPorosity`** *(possibly outdated)* — generates `porosityF` and `Df` fields from medium parameters (particle diameter, tortuosity, permeability). Editable medium description in `utilities/setPorosity/medium.H`. Kept for backward compatibility — for new cases prefer the `setFields` + STL workflow (see [Tips](#biomass-distribution-initial-porosityf)).
- **`totalMassPorousGasificationFoam`** — post-processing diagnostic that writes `totalMass.txt` (time, integrated solid mass `∫ρ_s · (1−porosityF) dV`) for the run. Operates on a reconstructed case — run `reconstructPar` first if the case was decomposed.

