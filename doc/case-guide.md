# Case guide

A PGF case is a normal OpenFOAM case plus extra files for the solid and its chemistry. This page walks through setting one up and running it, then lists what is easy to get wrong. Every key it mentions is in the [case reference](case-reference.md); the cases to start from are in [tutorials](tutorials.md).

## Your first case

Start from `tutorials/cases/canonical/pyrolysis`: one solid (`wood`), one reaction (`wood = targas`), a 1-D mesh, about a second to run.

1. **Copy it:** `pgf copy canonical/pyrolysis myCase` puts it in `$PGF_CASE_ROOT/<branch>/myCase` (by default `~/OpenFOAM/<user>-v2606/run/<branch>/myCase`) and prints the path; it copies the files the case links from its family too. Its `Allrun` restores `0/` from `0.orig/`, runs `blockMesh`, then the solver. `reference/` and `system/regressionFunctions` are for the regression tests and can go. If the source was run in place, the copy carries its output along; `./Allclean` it first, since `runApplication` skips every step whose `log.*` exists. `pgf copy --list` lists the tutorials; every option: [`pgf help copy`](pgf-reference.md#pgf-help-copy).
2. **Mesh and fields:** edit `system/blockMeshDict` and `0.orig/` ([required fields](case-reference.md#required-initial-fields)). Values here are uniform; for a partial bed use `setFields` ([how](#biomass-distribution-initial-porosityf)).
3. **Solid:** list components in `constant/solidThermophysicalProperties`, with skeletal (not bulk) density ([why](#solid-thermophysical-properties-skeletal-density-not-bulk)). Each needs a `Y<name>` field.
4. **Chemistry:** set gas `species` and `solidReactions` in `constant/chemistryProperties` ([reference](case-reference.md#constantchemistryproperties)). No reactions means both phases stay inert.
5. **Gas species:** each needs a field in `0.orig/` and an entry in `constant/thermo.compressibleGas` ([no JANAF data?](#gas-species-without-janaf-data)).
6. **Run:** `pgf run <the copy>`, then check `log.porousGasificationFoam`, or look at it with `pgf view <the copy>` ([ParaView](paraview.md)). If it is slow, see [time-step coupling](#time-step-coupling-between-gas-and-solid-chemistry). Every option: [`pgf help run`](pgf-reference.md#pgf-help-run); how pgf picks the build and records the run: [the pgf tool → Runs](pgf.md#runs).

Every tutorial `Allrun` / `Allclean` changes into its own directory first, so `/path/to/case/Allrun` also works from any directory in a shell that already has the environment (`pgf shell`, or a manually sourced OpenFOAM).

## Setting up a DEM case

A DEM case couples the solid to YADE bodies: YADE moves them, and PGF sends back how each body's size changes ([the idea](theory-DEM.md#the-idea)).

1. **Build.** You need a solver built with `pgf build --yade` and a YADE built from yade-pgf ([install](install.md#3-dem-coupling-optional)). `pgf doctor --yade` checks both.
2. **Start from the template.** Copy `MicroTGA-DEM_lambdaDotApparentVolume`; its [README](../tutorials/cases/MicroTGA-DEM_lambdaDotApparentVolume/README.md) explains every file, what to change and what to keep. `MicroTGA-DEM_charCO2` shows how one variant differs from it.
3. **Turn the coupling on.** `constant/yadeProperties` with `active true` ([reference](case-reference.md#constantyadeproperties)).
4. **Choose how λ changes.** `lambdaMode` in `constant/lambdaDict` ([keys](case-reference.md#constantlambdadict), [models](theory-DEM.md)). `apparentVolume` needs `rhoApparent` for every solid specie. For `exactDifferential`, a physically defensible starting `dlambdaOverDYmi` is nearer `1e-3` [m⁴/kg], the order of magnitude for a particle losing roughly a third of its length over full conversion of ~140 kg/m³ of solid. The fixtures use values ~400× smaller, tuned to keep both terms visible at their short-run scale: do not copy them into a real case.
5. **Set the connected threshold.** `solidRegimes/connectedAboveSolidFraction` in `pyrolysisProperties` decides where the solid counts as connected, which also gates `lambdaDot` ([solid regimes](theory.md#solid-regimes)).
6. **Match the time steps.** `system/controlDict` sets the run's time; the DEM script keeps pace with it. Each `lambdaMode` needs its own ratio between YADE's `O.dt` and `deltaT`: [DEM exchange → Keeping time](dem-exchange.md#keeping-time).
7. **Decompose and run.** The case must be decomposed into as many `processor*` directories as OpenFOAM ranks. The template's `Allrun` does this and calls `pgf dem run`; run the copy with `pgf run`, which picks the `--yade` build by itself for a case with an active `constant/yadeProperties` ([`pgf help dem`](pgf-reference.md#pgf-help-dem), [start and stop](dem-exchange.md#start-and-stop)). Run straight from a shell without a `--yade` solver or without `yade`, a DEM `Allrun` stops with exit 2 and `run this via: pgf run <case>`.

## Tips for preparing new simulations

Practical notes that are easy to get wrong on a first PGF case.

### Biomass distribution (initial `porosityF`)

Two approaches, in increasing order of complexity:

1. **`setFields`** (with or without `setSet`): the standard OpenFOAM workflow. Specify a `setFieldsDict` that sets `porosityF` inside a cell zone to the desired void fraction and leaves the rest at `1.0`. `setFields` alone is sufficient for simple geometries. Pair with `setSet` (batch mode) for more involved selection logic. Other fields (`T`, `Ts`, solid species mass fractions, …) can be initialised the same way. See `tutorials/cases/macroTGA_*` for an example chain (`blockMesh` → `setSet` → `refineHexMesh` → `setFields`).
2. **STL + `setFields`**: for non-trivial bed geometries, build an STL of the porous region in Salome or Blender and feed it to `setSet`/`setFields` via a `surfaceToCell` selector.

### Solid thermophysical properties: skeletal density, not bulk

Entries in `solidThermophysicalProperties` (`rho`, `K`, `Cp`, `Hf`) describe the **pure solid**, the material at `porosityF = 0`. Their `rho` is the skeletal density `ρ_s`: the material without its pores. The bulk density inside the bed is then `ρ_s · (1 − ε)`, which PGF reconstructs from `porosityF`. Literature often reports bulk values instead: feeding a bulk density into `rho` understates the solid by a factor of `(1 − ε)`.

`rhoApparent` is a third, separate quantity: the apparent (envelope) density of **one particle**, its mass over its own volume including internal pores. It lies below the skeletal `rho`, and it is not the bulk density of a packed bed, which also counts the gaps between particles. It is optional, except under `lambdaMode apparentVolume`, which needs it for every solid specie and stops at start-up naming any specie without it. Only the ratios between species matter there, so a consistent set matters more than the absolute values. A given value must be positive.

Material data lives in one database, `thermoData/solidThermoData`, one entry per material with its source; the file header states the basis of each value. A case includes it and maps its reaction names onto entries, e.g. `#include "${PGF_ROOT:-../../../..}/thermoData/solidThermoData"` and then `woodCoeffs { $park2010Wood; }`. pgf exports `PGF_ROOT` as the checkout, so a case copied anywhere still finds the database; the fallback after `:-` is a path relative to the including file, used when the case runs inside the checkout without pgf. Numerical fixtures may keep local values instead.

### Gas species without JANAF data

Pyrolysis pseudo-species (e.g. lumped `tar`, `targas`) generally have no JANAF coefficients. In `thermo.compressibleGas` pick an existing species from `$FOAM_ETC/thermoData` whose molar mass and `Cp(T)` are the best available proxy for the pseudo-species. Document the substitution in the case, so the assumption can be checked later.

### Heat of reaction at reaction temperature

`heatOfReaction` is taken with all participants at `Ts` and is not corrected with temperature. Give the value measured at reaction temperature (e.g. from a TGA/DSC fit), not a 298 K standard value ([theory → energy](theory.md#energy)).

### Radiation parameters are the hardest to source

The heterogeneous radiation coefficients (`a`, `as`, `borderAs`, `borderL`, `E`) rarely appear in literature with the right meaning. A workable approach is to tune them on a small TGA-like case so that the simulated heating rate matches an experiment, then reuse the tuned values across geometries that share the same surface chemistry and pellet morphology.

### Time-step coupling between gas and solid chemistry

Gas-phase reactions are typically orders of magnitude faster than heterogeneous solid reactions. When both are active, gas chemistry dominates the chemistry-limited time step and can make slow processes (e.g. gasification of a whole bed) very expensive.

- For slow heterogeneous processes, prefer chemistry mechanisms with the minimum gas-phase detail needed for the result you care about.
- `maxCo`, `maxDi`, and `initialChemicalTimeStep` in `controlDict` / `chemistryProperties` interact: tightening any one of them can mask the actual bottleneck. Check the solver log for which limit is binding before tuning ([how Δt is chosen](code-map.md#per-time-step-flow)).

### When the numbers go wrong

| Symptom | Likely cause | What to try |
|---|---|---|
| Negative temperatures | Time step too large, or aggressive reaction parameters | Reduce `deltaT`, `maxCo`, `maxDi` or `initialChemicalTimeStep` |
| Porosity above 1 or below 0 | Bed collapse too aggressive | Reduce `deltaT`, or adjust `solidRegimes/connectedAboveSolidFraction` in `pyrolysisProperties` |
| Gas mass fractions do not sum to 1 | Missing `Ydefault` field, or a gas species without its `0/` file | Check that every gas species has a `0/` file, and that `0/Ydefault` exists |
| Slow pressure convergence | Tight PIMPLE settings | Increase `nCorrectors`, or relax the `p` tolerance |

## Related pages

- [Tutorials](tutorials.md): the cases to start from.
- [Case reference](case-reference.md): every field and dictionary, with the source that reads it.
- [Viewing a case in ParaView](paraview.md): the case's views, comparing cases, PNGs.
- [The pgf tool](pgf.md): settings, builds and runs.
- [All documentation](README.md)
