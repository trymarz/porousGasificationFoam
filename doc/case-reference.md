# Case reference

The fields and files a PGF case adds to a normal OpenFOAM case, and their keys. Each section names the source that reads it; the full key list and the defaults are there. **If this page and the code differ, the code wins.** How to put a case together: [case guide](case-guide.md). What the quantities mean physically: [theory](theory.md).

Source paths are under `porousGasificationMedia/` unless they start with `porousGasificationFoam/`.

## Required initial fields

Read by `porousGasificationFoam/createFields.H` and the models that own each field.

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
| `Us` | volVectorField | Optional. Solid velocity [m/s], fixed in time; zero when the file is absent. With DEM it comes from YADE each step. |
| `anisotropyK` | volTensorField | Optional. Anisotropic multiplier on the solid conductivity: `K_eff = K · (1−φ) · anisotropyK`. Unit tensor when the file is absent. |
| `<gasName>` (e.g. `O2`, `N2`, `CO`) | volScalarField | Gas species mass fractions |
| `Y<solidName>` (e.g. `Ywood`, `Yash`) | volScalarField | Solid species mass fractions |
| `Ydefault` | volScalarField | Default field for unmatched gas species (set to 0) |
| `YsDefault` | volScalarField | Default field for unmatched solid species (set to 0) |

- `Df` is a tensor field. For isotropic porous media, use a diagonal tensor with large values (e.g. `1e9`) in gas-only regions and smaller values (based on permeability) in porous zones. Initialise it with `setFields` ([case guide → initial porosity](case-guide.md#biomass-distribution-initial-porosityf)).
- Solid species field names: `Y` + component name from `solidThermophysicalProperties` (e.g. `wood` → `Ywood`).
- Gas species field names match the `species` list in `chemistryProperties`.
- A case with radiation also needs `G`. For a DEM case, start from the template case ([case guide → a DEM case](case-guide.md#setting-up-a-dem-case)).

## Solid boundary conditions

Checked at start-up and during the run by `pyrolysisModels/pyrolysisModel/volPyrolysis/solidDiagnostics/`.

Solid crossing a patch carries mass (`Ym`, from the `Y<solidName>` boundary conditions) and enthalpy (`solidH`, from the `Ts` boundary condition). A patch therefore either **declares** the solid state or **inherits** it from the face cell:

| Solid `Yi` on the patch | `Ts` on the patch | Meaning |
|---|---|---|
| all fixedValue | fixedValue | declares: solid may enter with this composition and temperature (an inlet) |
| none fixed | fixedValue | inherits the composition; a heated wall |
| none fixed | not fixed | inherits everything; a wall or an outlet |
| some fixed, or all fixed without `Ts` | | rejected at start-up |

- Solid may enter only through a declaring patch whose fixed `Yi` sum to more than zero. Solid entering an inheriting patch would copy the face cell's mass and enthalpy in; solid entering faces whose fixed `Yi` sum to zero would bring enthalpy without mass.
- Solid must neither enter nor leave through a patch of mesh type `wall` (in `blockMeshDict`): give it `Us` `slip` or `fixedValue (0 0 0)`. Declare inlets and outlets as `type patch`.
- `solidBoundaryPolicy` (in `pyrolysisCoeffs`) decides what happens when solid crosses a patch it should not: `error` (the default) stops the run, `warn` reports it once per patch, `ignore` says nothing.
- A crossing counts once it exceeds `solidStateCheckTolerance` of the face cell's volume in a step. Solid moving in from a trace or empty face cell carries no mass and is not reported.
- Fixed `Yi` may sum to zero while no solid enters, e.g. an inlet fed later. If they sum to neither zero nor 1, the run warns and normalises them.
- A wall the solid does not cross needs no fixed `Yi`: use `zeroGradient`.
- The log lists the declaring, inheriting and wall patches at start-up.

## `constant/chemistryProperties`

Read by `thermophysicalModels/porousSolidChemistryModel/` (`basicPorousChemistryModel`, `ODESolidHeterogeneousChemistryModel`). The solid chemistry: its solver, the gas species it exchanges and the solid reactions.

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

**Reaction parameters**: `(A Ta Tcrit heatOfReaction n1 n2 ...)`

| Parameter | Meaning | Units |
|---|---|---|
| `A` | Pre-exponential factor | varies |
| `Ta` | Activation temperature `Ea/R` | [K] |
| `Tcrit` | Minimum temperature for reaction | [K] |
| `heatOfReaction` | Heat released/absorbed (<0 = exothermic), per kg of solid substrate | [J/kg] |
| `n1, n2, ...` | Reaction order for each LHS species in order | — |

- Rates are per kg of solid substrate, and every coefficient is a mass per kg of solid substrate. What that means for a coefficient on a lone solid, for gas substrates and under `diffusionLimitedReactions`: [theory → solid reaction rates](theory.md#solid-reaction-rates).
- `heatOfReaction`: which value to give, [case guide → heat of reaction](case-guide.md#heat-of-reaction-at-reaction-temperature).
- `solidReactionEnergyFromEnthalpy` picks how the reaction heat is computed (default `true` in the code; the tutorials mostly set `false`): [theory → reaction heat](theory.md#energy).

## `constant/solidThermophysicalProperties`

Read by `thermophysicalModels/HGSSolidThermo/`. The solid components and their properties.

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
    density         { rho         1050; rhoApparent 630; }   // [kg/m³] each; rhoApparent optional
};

ashCoeffs { ... }
```

`rho` is the skeletal density and `rhoApparent` the apparent density of one particle ([theory → notation](theory.md#notation); [why it matters](case-guide.md#solid-thermophysical-properties-skeletal-density-not-bulk)). Three `thermoType` variants (`HGSSolidMixtureThermos.C`):

| Variant | Cp(T) | Transport | Notes |
|---|---|---|---|
| `constHeterogeneous` | constant | constant | Simplest, adequate for most cases |
| `expoHeterogeneous` | `c0·(T/Tref)^n0` | exponential | T-dependent Cp and K |
| `linearHeterogeneous` | constant | linear | T-dependent K = a·T + b |

## `constant/pyrolysisProperties`

Read by `pyrolysisModels/pyrolysisModel/heterogeneousPyrolysisModel/` and `volPyrolysis/` (`solidRegimes` by `volPyrolysis/solidState/solidState.C`).

```cpp
active          true;
heterogeneousPyrolysisModel  volPyrolysis;

pyrolysisCoeffs
{
    subintegrateHeatTransfer true;
    frozenTemperature       false;       // optional; true: no energy solve
    bedCollapse             false;       // enable bed motion
    replenish               false;       // fuel replenishment model

    solidRegimes                         // optional; the defaults are shown
    {
        dispersedAboveSolidFraction  1e-8;
        connectedAboveSolidFraction  1e-4;
        maxSolidFraction             0.9999;
        dispersedSolidTemperature    solid;   // solid | gas
    }
    solidStateCheckTolerance 1e-8;       // optional
    solidBoundaryPolicy     error;       // optional: error | warn | ignore
}
```

- `frozenTemperature true` solves no energy equation ([theory](theory.md#energy)). The old key `equilibrium` still works but prints a deprecation warning.
- `solidRegimes`: what each regime means and which parts of the model each threshold switches: [theory → solid regimes](theory.md#solid-regimes). The thresholds must nest, `dispersedAboveSolidFraction` < `connectedAboveSolidFraction` < `maxSolidFraction` < 1, or the run stops at start-up. DEM cases must set `connectedAboveSolidFraction`.
- `solidStateCheckTolerance` is the slack the fail-fast checks and the limiter report allow, in solid fraction. It changes no result, only whether a run warns or aborts.
- The log prints the regimes with their values at start-up, and every write time holds `solidRegime` (0 empty, 1 trace, 2 dispersed, 3 connected) and `solidPresent` (1 where α > 0), both as at the start of the step: colour by `solidRegime` in ParaView to see the regimes.
- The keys `solidRegimes` replaces (`criticalPorosity`, `minPorosity`, `solidStateTolerance`, `gasTemperatureBelowCriticalPorosity`) stop the run with the exact replacement; `connectedAboveSolidFraction` is 1 − `criticalPorosity` and `maxSolidFraction` is 1 − `minPorosity`.

## `constant/yadeProperties`

Read by `porousGasificationFoam/createDEMFields.H`. `active true` turns the DEM coupling on; it needs a `WITH_YADE` build (`pgf build --yade`), and `pgf run` then picks that build without asking. Without the file, or with `active false`, the case runs without DEM.

## `constant/lambdaDict`

Read only when the DEM coupling is active, by `DEM/lambdaDotModel.C` (the mode and how the DEM fields are smoothed onto the mesh) and `pyrolysisModels/pyrolysisModel/volPyrolysis/demCoupling/` (which model computes `lambdaDot`). The models are in `DEM/lambdaDotModels/`. What each mode sends and why: [DEM coupling](theory-DEM.md); the exchange: [DEM exchange](dem-exchange.md).

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
//    YADE multiplies lambda by once per exchange (1 = no change); needs each
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

- `lambdaMode` defaults to `constant` when the key is absent.
- Leaving every `exactDifferential` coefficient at its `0.0` default gives `lambdaDot = 0`, which is why there is no separate "off" mode. The equations and the dimensions of the coefficients: the `Description` at the top of `DEM/lambdaDotModels/exactDifferentialLambdaDot/exactDifferentialLambdaDot.H`. Choosing a value: [case guide → a DEM case](case-guide.md#setting-up-a-dem-case).
- Both interpolation solves look up their linear-solver controls under `system/fvSolution`'s `solvers` by the name of the field being smoothed, `Us` and `lambda`, and fall back to in-code controls (`smoothSolver`/`symGaussSeidel`, `tolerance 1e-6`, `relTol 0.01`) when the entry is absent. The name `UsDEMInterpolation` is not consulted; the committed cases match both names with one regex entry.
- `system/controlDict` alone sets a coupled run's time; the step ratio each `lambdaMode` needs: [DEM exchange → Keeping time](dem-exchange.md#keeping-time).

## `constant/heatTransferProperties`

Read by `thermophysicalModels/solid/heatTransfer/`; models `constCONV`, `cylinderCONV`, `pipeCONV`.

```cpp
heatTransferModel constCONV;
Parameters
{
    h       8;        // Heat transfer coefficient [W/m²/K]
    SAV     468;      // Specific surface area [1/m]
}
```

With `constCONV`, the volumetric coefficient is `h_v = h · SAV` [W/m³/K] ([theory → convection](theory.md#energy)).

## `constant/specieTransferProperties`

Read by `thermophysicalModels/solid/specieTransfer/`, only when `diffusionLimitedReactions true`; models `constST`, `cylinderST`, `pipeST`.

```cpp
specieTransferModel constST;
Parameters
{
    h       0.017;    // Mass transfer coefficient
    SAV     468;      // Specific surface area [1/m]
}
```

## `constant/porosityProperties` (optional)

Read by `fieldPorosityModel/fieldPorosityModel.C`. Adds the Forchheimer term to the momentum sink ([theory → momentum](theory.md#gas)). Without the file, `forchheimerCoeff` is `0` and only the Darcy term `Df·U` applies.

```cpp
forchheimerCoeff 3.21e6;
```

Unlike `Df` (a per-cell field), `forchheimerCoeff` is one scalar for the whole mesh. It matters for fast flow through coarse beds; for slow flow in a fine porous medium the Darcy term is usually enough.

## `constant/radiationProperties`

Read by `radiationModels/heterogeneousRadiationModels/`.

```cpp
radiation       on;
heterogeneousRadiationModel  heterogeneousP1;
solverFreq 1;

heterogeneousAbsorptionEmissionModel heterogeneousConstantAbsorptionEmission;
heterogeneousConstantAbsorptionEmissionCoeffs
{
    a               a  [ 0 -1  0 0 0 0 0 ] 0;       // Gas absorption coefficient [1/m]
    as              as [ 0 -1  0 0 0 0 0 ] 0;       // Scattering coefficient [1/m]
    borderAs        borderAs [ 0 -1  0 0 0 0 0 ] 180; // Surface absorption [1/m]
    E               E  [ 1 -1 -3 0 0 0 0 ] 0.0;     // Extra constant gas sink [W/m3]
    borderL         borderL  [ 0 1 0 0 0 0 0 ] 1.5e-3; // Surface layer [m]
}
```

What the gas receives under `heterogeneousP1`, and the role of `E`: [theory → radiation](theory.md#energy). Leave `E` at 0 unless you mean to remove heat. Sourcing the coefficients: [case guide](case-guide.md#radiation-parameters-are-the-hardest-to-source).

## `system/` files: key settings

```cpp
// system/fvSolution
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

// system/fvSchemes
divSchemes { div(phiSolid) Gauss upwind; }

// system/controlDict
application     porousGasificationFoam;
adjustTimeStep  yes;
maxCo           5;          // Courant number limit (fluid)
maxDi           5000;       // Diffusion number limit (solid)
```

- `fvSolution`: the `Ts` (solid temperature) solver needs a tight tolerance (1e-10) for stability.
- `fvSchemes`: `div(phiSolid)` is the scheme of the solid's own transport (`pyrolysisModels/pyrolysisModel/volPyrolysis/porosityRecovery/porosityRecovery.C`); most tutorials use `upwind`, a few `limitedLinear` or `SuperBee`.
- `controlDict`: how `maxCo` and `maxDi` set Δt: [code map → per-time-step flow](code-map.md#per-time-step-flow), step 1.

## Related pages

- [Case guide](case-guide.md): putting a case together, and tips.
- [Tutorials](tutorials.md): the cases to start from.
- [Theory](theory.md): what the keys mean physically.
- [All documentation](README.md)
