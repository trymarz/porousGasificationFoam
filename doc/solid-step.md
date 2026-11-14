# Solid step

What `volPyrolysis::evolveRegion()` does in one time step, stage by stage, in the order the code runs them. This is step 4 of the [per-time-step flow](theory.md#per-time-step-flow). The source is `porousGasificationMedia/pyrolysisModels/pyrolysisModel/volPyrolysis/volPyrolysis.C`; each module lives in a subdirectory next to it. **If this page and the code differ, the code wins.**

The reads and writes come from the "Field access" list in each module's header; the kinds come from the table in [#239](https://github.com/trymarz/porousGasificationFoam/issues/239):

| Kind | Access |
|---|---|
| Model | reads fields; returns a result or writes only what it owns |
| Equation | writes the field it solves and what derives from it |
| Constraint | a declared writer of solid fields, called explicitly by volPyrolysis |
| State update | recomputes the derived solid state |
| Diagnostics | reads only; reports, warns or aborts |

volPyrolysis owns the solid fields: `Ym` (solid mass per cell volume, one per specie), `Ys` (mass fractions), `solidH`, `Ts` and `porosity`. "SolidState masks" are `solidPresent`, `solidRegime` and the gate `connectedAtStepStart`, all owned by `SolidState`.

## The stages

Each stage reads what the stages above it left, so the order sets the results.

### Rates and exchange, from the step-start state

**1. `markReactingCells()`** — volPyrolysis, setting up the chemistry model.
- Reads: the gate `connectedAtStepStart`.
- Writes: the chemistry's per-cell reacting flag. Only cells that are active and connected at step start react, so the solid energy equation books every reaction heat.
- Checks: none.

**2. `updateTransferCoefficients()`** — model (`GasSolidHeatExchange`, specie transfer).
- Reads: for `GasSolidHeatExchange`, `Ts`, `rho`, `porosity`, `porosityF0` and the gas thermo; the specie transfer model (`ST()`) has no "Field access" list.
- Writes: the heat transfer coefficient (owned by `GasSolidHeatExchange`, skipped under `equilibrium`) and volPyrolysis's `specieTransferCoeff`.
- Checks: none.

**3. `solveChemistry()`** — model (solid chemistry).
- Reads: the chemistry model's own inputs; it has no "Field access" list.
- Writes: the chemistry's sources (`RRs`, `RRg`, `RRpor`), and volPyrolysis's `chemistrySh` and chemistry time step.
- Checks: none.

**4. `exchangeHeatWithGas()`** — model (`GasSolidHeatExchange`).
- Reads: `Ts`, `rho`, `porosity`, `porosityF0`, the gas `T` and `hs_i`, the gate, and this step's `Srho_i`, the same per-species sources that `rhoEqn.H` and `YEqn.H` read.
- Writes: only what it owns: `interfaceEnthalpy`, `agentHeatUp`, and the gas-solid heat transfer (unless in `equilibrium` or inactive).
- Checks: none.

### Solid flux

**5. `limitSolidFlux()`** — model (`SolidFluxLimiter`).
- Reads: `Us`, `Ym`, `rho`, `Ts`, the chemistry's `RRs` and `RRpor`, the SolidState thresholds.
- Writes: nothing itself; `limit()` returns the flux, which volPyrolysis stores in `phiSolid`. Before that, when limiting is on, volPyrolysis refreshes the `Ym` patches, since the bed motion and `updateSolidState()` of the last step changed what they depend on.
- Checks, right after: **`SolidDiagnostics::checkSolidCrossing()`** (diagnostics) on the limited flux, before any solid field moves. It warns, once per patch, about solid entering an inheriting patch, solid entering declaring faces whose `Yi` sum to zero, and solid crossing a wall-type patch.

### Species, then energy

**6. `SolidSpeciesTransport::solve()`** — equation.
- Reads: `phiSolid`, the chemistry's `RRs`; `Us` and `porosity` only for the abort report.
- Writes: `Ym` (solved, clipped, then its patches corrected), `Ys` from `Ym`, and the per-specie mass fluxes `phiYm` for the energy step. Through `DemCoupling` it resets `lambdaDot` and adds the chemistry term, with `Ym_i` taken before it is solved.
- Checks, per specie after its solve: **`checkSpecieMass()`** aborts on an impossible `Ym_i`; then negative mass is clipped to 0 and booked in `clippedMass`.

**7. `SolidEnergy::solve(phiYm)`** — equation.
- Reads: `Ym`, `rho`, `K`, the `phiYm` from stage 6, the radiation, reaction and exchange sources, the gas `T`, the SolidState masks and gate; `Us` and `porosity` only for the abort report.
- Writes: `Ts` (the unknown), the solid thermo through `solidThermo.correct()` (which sets the `rho` read by stages 7a and 9), and its own `radiationSh`.
- Checks: **`abortOnInvalidTs`** after the enthalpy balance, on cells holding more than trace mass.

**7a. `classifyPostStep()`** — state update (`SolidState::classify()`).
- Reads: `Ym`, `rho` from stage 7, the porosity they give (the expression stage 9 writes), and `solidPresent` as the step started.
- Writes: `solidRegime` and the gate `connectedAtStepStart` for the next step, together. This is the step's one classification; both regime rules read it.
- Checks: none.

**7b. `SolidEnergy::finishStep()`** — equation.
- Reads: the classification from stage 7a, the gas `T`.
- Writes: `Ts`: the gas `T` where the cell holds trace solid or its reaction emptied it this step, and, with `dispersedSolidTemperature gas`, where it holds no connected solid; then `solidH` from `Ts` on the mass that remains.
- Checks: **`abortOnInvalidTs`** after the regime rules, on all cells.

**8. `DemCoupling::addTemperatureTerm()`, `finishStep()`** — model (DEM `λ̇`). No-ops unless built with Yade and DEM is active.
- Reads: this step's `Ts`.
- Writes: only `lambdaDot`, which it owns through the registry: the temperature term, then its boundary values once every term is in.
- Checks: none.

### Porosity and bed

**9. `PorosityRecovery::updatePorosity()`** — state update.
- Reads: `Ym`, `rho` (from stage 7), and the chemistry's `RRpor` at this step's `Ts`.
- Writes: `porosity` on the internal field, from `1 − porosity = Σᵢ Ymᵢ/ρᵢ` (a fixedValue patch keeps its value); and its own porosity source from `RRpor`, which is reported and never applied.
- Checks: **`guardPorosity()`** aborts if any cell's solid fraction leaves `[0, maxSolidFraction]`.

**10. `moveBed()`** — constraint.
- Reads: `Ym`, `porosity`, `Us`, `phiSolid`, and the gradient of last step's `solidPresent`, to find the flip cells (`PorosityRecovery::findFlipCells()`).
- Writes: then `SolidState::refreshSolidPresent()`, and one of:
  - with `bedCollapse` active, `BedCollapse::collapseOnto()` writes `porosity`, `Ts`, `Ym`, `Ys`, `solidH`, and also `porosityF0`, `rho` and the `solidPresent` patches, which move with the column; `Ys` is rederived from `Ym` if anything collapsed;
  - otherwise, with `emptyFlippedCells`, `PorosityRecovery::emptyCells()` writes `Ym`, `Ys`, `solidH`, `Ts` and `porosity` of the flipped cells; the mass is destroyed and booked in `flippedMass`;
  - otherwise, nothing.

  Last, `solidPresent` is refreshed again for the checks.
- Checks: the "solid flip guard" line in the log; the checks follow as stage 11.

**11. `checkPorosity()`** — diagnostics.
- Reads: `porosity`, `Ym`, `rho`, `solidPresent`.
- Writes: nothing.
- Checks: **`guardPorosity()`** again, after the bed motion; **`SolidDiagnostics::checkConsistency()`** prints the worst cell violating `1 − porosity = Σᵢ Ymᵢ/ρᵢ` and warns past tolerance.

**12. `updateSolidState()`** — state update.
- Reads: `porosity`, `Ym`, `rho`, `solidPresent`.
- Writes: the `porosity` patches; if stage 10 rewrote any cell, `solidRegime` and the gate again, from what the motion left (the `Ts` rules are not re-applied); the `solidPresent` and gate patches; `cellOnBedBorder` (`PorosityRecovery::markBedBorderCells()`).
- Checks: none.

### Reports

**13. `reportStep()`** — diagnostics.
- Reads: `clippedMass`, `flippedMass`, `collapsedMass`, `chemistrySh`, `Ys`.
- Writes: only the run totals.
- Checks: none; it prints the mass budget (transported solid mass against the booked losses), the range of each derived mass fraction, and `volPyrolysis::info()`.

## Exceptions to the table

These are recorded in [#249](https://github.com/trymarz/porousGasificationFoam/pull/249):
- volPyrolysis refreshes the `Ym` patches before the limiter (stage 5), because the bed motion writes `Ym` after the species stage.
- `BedCollapse` also writes `porosityF0`, `rho` and the `solidPresent` patches.
- `PorosityRecovery` is both a state update (stages 9 and 12) and a constraint (stage 10).
- `SolidEnergy` keeps a writable solid thermo for `correct()`.
