# Regression metric ledger

One row per registered case and function object: what the metric can detect, what it cannot, and whether it is doing its job. Read with [regression-testing.md](regression-testing.md), which explains how the comparison works. Tracking issue: #69 (step 3).

Built from the committed `reference/` files on `main` at 5cec447, each case's `system/regressionFunctions` and its setup. Cases with a third column in `cases.list` (`expectFail`, `expectLog`) check a log message, not metrics, and are not listed.

## Status

| Status | Meaning |
|---|---|
| live | Varies in the baseline and follows the behaviour the case is about. |
| invariant | Flat by design: a conserved total, a bound, a prescribed value. The baseline is the expected value; step 4 decides whether it becomes an explicit check. |
| diluted | Varies, but most of the domain holds no solid and sets the value. |
| dead | Cannot move for any change in the behaviour it claims to watch. |
| question | Moves, or stays flat, in a way the case does not explain. Needs a run before the baseline is trusted. |

"Flat" means the whole column changes by less than `rtol` (1e-4). A flat column still fails if its level shifts; what it cannot do is follow anything in time.

## Findings

1. **Wood integral is the domain volume** (8 cases). `canonical/pyrolysis` and its siblings have one solid component, so the mass fraction `Ywood` is 1 everywhere and `volIntegrate(Ywood)` is the mesh volume, 1.6e-06 m³. Pyrolysis mass loss is `volIntegrate(Ywoodm)`, as `conservation/chemistryGateCrossing` already does.
2. **Radiation does not reach the solid** in `canonical/radiation` and `canonical/meanTemp`. Their `Ts` and `porosityF` columns equal `canonical/pyrolysis` (radiation off) to all 6 printed digits. Absorption is `borderAs * bedBorderSurfaceAreaDensity` with `a = as = 0`, and a uniform `porosityF = 0.4` bed has no border cells. Only `G` moves. To confirm: write `bedBorderSurfaceAreaDensity` and check it is zero.
3. **Empty cells set the solid temperature averages** in the char-only moving cases. `serial`, `serial_m2`, `serial_m4` and `solidInlet` hold no solid in most cells, where `Ts = 0` by design: `min(Ts)` is 0 for the whole run and `volAverage(Ts)` is 24–120 K. #176 fixes `solidInlet_tinyDt` with a `Ycharm`-weighted average, as #162 did for `rightWallSolidAccumulationFromLeftInlet`.
4. **Species that do not exist.** `volIntegrate(Yhumm)` and `volIntegrate(Yrubberwoodm)` are 0 in all six char-only cases (no initial mass, reactions commented out), as is `Yashm` in `solidInlet` and `solidInlet_tinyDt`. `min/max(N2)` in `rightWallSolidAccumulationFromLeftInlet` are 1 (single-species gas).
5. **The char-only `serial` claims do not hold.** "Increases as the solid bed burns away": char oxidation has `Ta = 1.96e4 K` and `Ts` peaks at 529 K, so the rate is about 6e-8 1/s and nothing burns in 2 s. `volAverage(porosityF)` is flat and `volIntegrate(Ycharm)` moves less than 1e-4. The case tests motion and heat-up; the char integral works as a mass-conservation invariant.
6. **`conservation/gasPressureWork` gas energy residual drifts.** The comment says it "stays at its t = 0 value". From t = 0.05 to 0.5 it moves by 2.8e-06, 0.87% of the 3.3e-04 the gas received, growing steadily. The first-order running mean `heatTransferMean` may explain it; to settle, halve `deltaT` and see whether the drift halves.
7. **`canonical/pyrolysis` gas pressure** rises from 2.1 bar to 39 bar in 0.5 s. Plausible for a sealed box filling with pyrolysis gas, but no metric or comment says so.
8. **`canonical/gasification`** (on `main`): `volIntegrate(Ychar)` is flat and `volAverage(CO2)` is exactly 0. #174 makes the gas reaction run and recaptures the baseline; the rows below are `main`'s.
9. **No case can see the #31 inflow guard.** It fires in `prescribedUsCases/tetrisCompaction`: 36209 candidates, 17243 vetoed. With `poroProtectSolidInflowFluxTolerance 1e30` none are vetoed, and all six metrics still match the baseline. The case sets neither `bedCollapse` nor `emptyFlippedCells`, and `PorosityRecovery::recover()` uses the flip list only under one of them, so the guard decides nothing there. The same case with `emptyFlippedCells true` does see it: the guard lets 231 flips destroy 2.4e-08 kg of solid; off, 7319 flips destroy 9.7e-08 kg, and `volIntegrate(Ycharm)` moves by 5e-04 relative. That variant is the fixture #175 asks for; #187 adds it as `prescribedUsCases/tetrisCompactionEmptying`.
10. **The comparator ignores unreferenced output.** It walks `reference/` and fails on a reference file missing from the run, but a `.dat` with no reference is never read. A newly added metric passes unchecked until someone captures it.

## Ledger

### charOnlyMoveCases

`serial`, `serial_m2`, `serial_m4` (same case, mesh refinements; identical rows):

| Function object | Status | Detects | Cannot detect |
|---|---|---|---|
| `PorosityAverage` `volAverage(porosityF)` | invariant | solid volume created or lost | where the solid is; any burning (none happens) |
| `PorosityMin` `min(porosityF)` | live | packing of the densest cell as the bed moves | — |
| `SolidSpeciesIntegrals` `Ycharm` | invariant | char mass created or lost in transport | burning (none happens) |
| `SolidSpeciesIntegrals` `Yashm` | invariant | ash mass created or lost | — |
| `SolidSpeciesIntegrals` `Yhumm`, `Yrubberwoodm` | dead | nothing: zero mass, reactions off | — |
| `TsAverage` `volAverage(Ts)` | diluted | gross heating | solid temperature (most cells are empty, `Ts = 0`) |
| `TsMax` `max(Ts)` | live | peak solid temperature | — |
| `TsMin` `min(Ts)` | dead | nothing: 0 in empty cells | — |

`solidInlet`:

| Function object | Status | Detects | Cannot detect |
|---|---|---|---|
| `PorosityAverage`, `PorosityMin` | live | inflow filling the domain | — |
| `SolidSpeciesIntegrals` `Ycharm` | live | char mass entering | — |
| `SolidSpeciesIntegrals` `Yashm`, `Yhumm`, `Yrubberwoodm` | dead | nothing: the inlet brings only char | — |
| `TsAverage` | diluted | — | solid temperature (24 K while empty cells dominate) |
| `TsMax` | invariant | heating above the inlet's 300 K | — |
| `TsMin` | diluted | — | 0 until the domain fills |

`solidInlet_tinyDt` (as in #176): `PorosityAverage`, `PorosityMin`, `Ycharm` live; `Yashm`, `Yhumm`, `Yrubberwoodm` dead; `TsMax` and `TsCharWeighted` invariant at 300 K, checked as `== 300` (#188). It does not exercise the #31 inflow guard: 0 candidates (#175).

`solidInlet_ash`:

| Function object | Status | Detects | Cannot detect |
|---|---|---|---|
| `InletSolid` `areaAverage(Yashm, solidH)` | invariant | the patch's declared composition and enthalpy (#46) | — |
| `PorosityAverage`, `PorosityMin` | invariant | — | inflow: flat at 0.5 |
| `SolidSpeciesIntegrals` `Ycharm`, `Yashm` | live | char and ash mass entering | — |
| `SolidSpeciesIntegrals` `Yhumm`, `Yrubberwoodm` | dead | nothing | — |
| `TsAverage`, `TsMax`, `TsMin` | live | solid heating (269–489 K); the domain is full of solid | — |

### canonical

`pyrolysis`, and with the same rows `stoichiometric`, `enthalpy`, `noSubintegration`, `pyrolysisE` (pyrolysis solved for e; `TAverage` must match h's):

| Function object | Status | Detects | Cannot detect |
|---|---|---|---|
| `Average` `volAverage(Ts, targas)` | live | heat-up, tar produced | — |
| `TsMax`, `TsMin` | live | solid temperature range | — |
| `PorosityAverage` | live | mass converted to gas | — |
| `WoodIntegral` `volIntegrate(Ywood)` | dead | the mesh volume (finding 1) | pyrolysis mass loss |
| `TAverage` `volAverage(T, p)` (`pyrolysis`, `pyrolysisE` only) | live / question | gas heating and pressure (finding 7) | — |

`equilibrium`: `Ts` 300 K everywhere for the whole run, flat by design (invariant); `WoodIntegral` dead.

`radiation`, `meanTemp`: `GAverage` live. `TsAverage`, `TsMax`, `PorosityAverage` are copies of `pyrolysis` (finding 2), so they cannot detect radiation reaching the solid: **question**. `WoodIntegral` dead.

`gasification` (main; see finding 8): `COAverage` live; `CO2Average` dead (0); `CharIntegral` flat (question: no char consumed); `TsAverage`, `PorosityAverage` flat within `rtol`.

`darcy`, `darcyE`, `forchheimer`:

| Function object | Status | Detects | Cannot detect |
|---|---|---|---|
| `PInlet`, `POutlet` `areaAverage(p)` | invariant | the steady pressure drop (Darcy / Forchheimer) | — |
| `MassInlet`, `MassOutlet` `sum(phi)` | invariant | the mass flow, and their sum the mass balance | — |
| `UAverage` | live | velocity settling | — |
| `TRiseAverage`, `TRiseMax` (`darcy`, `darcyE`) | live | viscous / pressure-work heating | — |

Their steady drop is also checked against Darcy–Forchheimer: `regressionDarcyForchheimer dpRatio == 1` in `system/regressionInvariants` (#179).

### canonical/solidRates

Ten single-cell, single-step heterogeneous rate fixtures (#209 block A), one per reaction shape of #201: `H1`/`H1s` solid only, `H2` solid + gas, `H3` the same diffusion-limited, `H4` solid → solid, `H5` two solid reactants, `H6` the §5.6 gasifier shape, `H7` `H2` with doubled coefficients, `H8s` and `H9s` stoichiometric. `solidRates.py check` recomputes each case's rates from its own input files and writes `regressionSolidRates`.

| Column | Status | Detects | Cannot detect |
|---|---|---|---|
| `rateRatioMin`, `rateRatioMax` | invariant (== 1, 1e-5) | any change to Eq. 4, 5, 10, the species split or the Eq. 12 heat source, for these shapes | a later step's rates; the time-step limiter |
| `massRatio` | invariant (== 1, 1e-9) | mass created or lost by the split | — |
| `source(<specie>)`, `heat` | live | the same, against the baseline | — |

The species split is the code's convention, written out in `split_coefficients()`; the #201 fix changes it there and in the solver together.

### canonical/conservation

These cases assert a balance; the comments in their `regressionFunctions` state each claim, and the data agree except where noted.

| Case | Assertion (invariant) | Liveness (live) |
|---|---|---|
| `interphaseHeatTransfer`, `…Plain` | `HeatTransferResidual` 1e-19–1e-18 | `HeatTransferExchanged`, `TsAverage` |
| `radiationReservoir` | `RadiationBookResidual` 0; `RadiationWallFlux` 0 (zeroGradient G) | `RadiationBookedMax/Min` ±2e7, `GAverage`, `TsAverage` |
| `chemistryGateCrossing` | `ChemistryHeatOutsideGate`, `GasSourceOutsideGate` 0; `PorosityMax` flat after the crossing | `SolidMass` (`Ywoodm`), `ChemistryHeat`, `TsMax`; `SolidSensible` and `GasTempMax` are reported for #105, not asserted |
| `gatedBandSources` | `RadiationSh`, `ChemistrySh`, `HeatTransfer`, `HeatUpGas`, `Srho` 0 | `TsMax` 1200, `GasTempMax` 300, `GAverage`, `PorosityMax`: flat by design, liveness is that they are far apart |
| `gasPressureWork` | `HeatTransferResidual` ~8e-16; `GasEnergyResidual` **question** (finding 6) | `HeatTransferExchanged`, `PAverage`, `TAverage`, `TsAverage` |

The comparator's `atol = 1e-12` is the effective threshold for the zero and roundoff columns.

### prescribedUsCases

All six metrics in every case: `volIntegrate`, `min`, `max` of `Ycharm`, and `volAverage`, `min`, `max` of `porosityF`.

| Case | `YcharmIntegral` | `PorosityAverage` | The rest |
|---|---|---|---|
| `converging`, `shear`, `vortexIrrotational`, `vortexOneOverR2`, `vortexRankine`, `vortexSolidBody` | invariant (closed, 7.224e-07) | invariant | live; `shear` `PorosityMin` moves less than `rtol` |
| `convergingJam`, `convergingJamTight` | invariant | invariant | `PorosityMin` and `YcharmMax` are flat at the cap (0.4 / 0.1, 630 / 945): invariant bounds; the rest live |
| `diverging` | live (solid leaves through the `zeroGradient` boundary) | live | live |
| `tetrisCompaction` | live | live | live; blind to the inflow guard (finding 9): `tetrisCompactionEmptying` is the fixture that sees it |
| `tetrisCompactionEmptying` (#187) | live; fails with the inflow guard off (finding 9) | live | live |
| `rightWallSolidAccumulationFromLeftInlet` | live (inlet) | live | see below |

`rightWallSolidAccumulationFromLeftInlet` also records per-component `min/max/volAverage` of `Ychar Yash Ywood Ymineral` (the maxima are flat at the inlet composition: invariant bounds), `TsCharWeighted` (invariant 300 K, #162), `min/max(rhos)`, `min/max(rho)`, `min/max(p)` (live), and `min/max(N2)` (dead, single-species gas).

## Next

- Step 4: turn the invariant rows into explicit checks (bounds, zero residuals, conserved totals, Darcy's law) instead of 100-row copies of one value.
- Replace the dead rows: `Ywoodm` for the wood integral; drop the absent species; weight `Ts` by solid in the char-only cases.
- Settle the questions: radiation reaching the solid (finding 2), the gas energy drift (6), pyrolysis pressure (7), gasification after #174 (8).
- Make the comparator report unreferenced output (finding 10).
