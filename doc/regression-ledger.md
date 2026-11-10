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

Twelve single-cell, single-step heterogeneous rate fixtures (#209 blocks A, B and H), one per reaction shape of #201: `H1`/`H1s` solid only, `H2` solid + gas, `H3` the same diffusion-limited, `H4` solid → solid, `H5` two solid reactants, `H6` the §5.6 gasifier shape, `H7` `H2` with doubled coefficients, `H8s` and `H9s` stoichiometric, `H10s` `H8s` diffusion-limited, `H11e` `H1` in enthalpy mode over several ODE sub-steps. `solidRates.py check` recomputes each case's rates from its own input files and writes `regressionSolidRates`.

The solver's sources are means over the step, so each hand value is the step-start rate times `(1 - e^-a)/a`. Here `a` is the rate's fall over the step, `sum_k (d ln r/d ln c_k)(-dc_k/dt) dt/c_k` over the reactants, computed from the hand rate and the case's inputs, never from solver output. `rateRatioMin` and `rateRatioMax` therefore compare the solver with that step mean, not with the start rate. Heat is a step mean too since it became an ODE state, so a heat source left at the step start reads `1 + a/2` on the 8 fixtures with a gas reactant and fails (7.4e-5 to 5.2e-4). The ratios sit within 1.2e-6 of 1. What is left is roundoff, and for `H3`/`H10s` a fixed 7.3e-7. The temperature's change over the step is left out of `a`; it is below 1e-7 here.

| Column | Status | Detects | Cannot detect |
|---|---|---|---|
| `rateRatioMin`, `rateRatioMax` | invariant (== 1, 1e-5) | any change to Eq. 4, 5, 10, the species split or the Eq. 12 heat source, for these shapes; a source that no longer follows the ODE state over the step | a later step's rates; the time-step limiter; a fall too small to see (`H1`, `H1s`, `H4`) |
| `massRatio` | invariant (== 1, 1e-6) | mass created or lost by the split | a loss below 1e-6. There is no gas rescale any more, so the column holds the species' roundoff, ulp(c) over the step's change, up to 1.8e-7 (`H10s`) |
| `rateRatioMin`, `rateRatioMax` (`H11e` only) | invariant (== 1, 5e-3) | an enthalpy-mode solid temperature that does not advance by each ODE sub-step's own heat (#209 H): 11.06 with the cumulative increment of `cb45c72` | Sh (no hand value in enthalpy mode); a move below 5e-3, the per-sub-step Ti update leaves the sources 1.05e-3 above the continuous hand value |
| `agentRatio` (`H10s` only) | invariant (== 1, 1e-4) | an Eq. 10 agent factor that does not match the agent's consumption: the gas reactant consumed over its Eq. 9 delivery at the diffusion limit (1.33 with the literal Eq. 10) | a move below 1e-4; it sits 9.0e-5 below 1, 1.5e-5 the kinetic resistance's share and 7.5e-5 the rate's fall over the step, against the step-start delivery |
| `source(<specie>)`, `heat` | live | the same, against the baseline | — |

The species split, written out in `split_coefficients()`, is Eq. 6 with the rate per kg of solid substrate (#201); a change to it in the solver has to change it there too.

`H1` and `H1s` run at 1e-6 s, not 1e-8 s. The heat state starts from `c Cp T`, 3.6e8 J/m³ here, and seulex's extrapolation amplifies its rounding to at most 6.5 or 31.5 ulp, depending on the column reached. At 1e-8 s that reaches 1.4e-5 of the step's heat, past the invariant; at 1e-6 s it stays below 8.8e-7.

### canonical/conservation

These cases assert a balance; the comments in their `regressionFunctions` state each claim, and the data agree except where noted.

| Case | Assertion (invariant) | Liveness (live) |
|---|---|---|
| `interphaseHeatTransfer`, `…Plain` | `HeatTransferResidual` 1e-19–1e-18 | `HeatTransferExchanged`, `TsAverage` |
| `radiationReservoir` | `RadiationBookResidual` 0; `RadiationWallFlux` 0 (zeroGradient G) | `RadiationBookedMax/Min` ±2e7, `GAverage`, `TsAverage` |
| `chemistryGateCrossing` | `ChemistryHeatOutsideGate`, `GasSourceOutsideGate`, `InterfaceEnthalpyOutsideGate`, `AgentHeatUpOutsideGate` 0; `PorosityMax` flat after the crossing | `SolidMass` (`Ywoodm`), `ChemistryHeat`, `TsMax`; `SolidSensible` and `GasTempMax` are reported, not asserted (`GasTempMax`: rigid-tank filling with gas at h(Ts), #212) |
| `gatedBandSources` | `RadiationSh`, `ChemistrySh`, `HeatTransfer`, `InterfaceEnthalpy`, `AgentHeatUp`, `Srho` 0 | `TsMax` 1200, `GasTempMax` 300, `GAverage`, `PorosityMax`: flat by design, liveness is that they are far apart |
| `interfaceEnthalpyPyrolysis` | `GasTempMax`, `TsMax` == 600 | `Srho`, `InterfaceEnthalpy` |
| `interfaceEnthalpyMixing` | `TsMax` == 700; `GasTempMax` is the hs mixing temperature to 1e-5 K (checked by its `Allrun`) | `Srho`, `InterfaceEnthalpy`, `TargasMax` |
| `interfaceEnthalpyGasification` | `GasTempMax`, `TsMax` == 900 | `COMax`, `O2Min`, `Srho`, `InterfaceEnthalpy`; `AgentHeatUp` 0 to roundoff |
| `gasPressureWork` | `HeatTransferResidual` ~8e-16; `GasEnergyResidual` **question** (finding 6) | `HeatTransferExchanged`, `PAverage`, `TAverage`, `TsAverage` |
| `gasReactionPorosity` (#226) | `energyRatioMin/Max`, `speciesRatioMin/Max` == 1 within 1e-8 (observed 1e-9; 2.5 = 1/eps with YEqn's unweighted `R(Yi)`); `massChange` 0 | `regressionGasState`: the cell's `N2 O2 CO CO2 T h rho p porosityF` per step |

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

## Recaptures

**#209 F.** `pyrolysis`, `pyrolysisE`, `gasification`, `conservation/interfaceEnthalpyGasification`, `conservation/chemistryGateCrossing` and the 11 `solidRates` fixtures, from build `2ff2cc0c020b`. Nothing else moved beyond `rtol`. Largest moves:

| Case | Column | Move | Why |
|---|---|---|---|
| `pyrolysis`, `pyrolysisE` | `volAverage(p)` | 2.7e-4 | both causes below; p is the only column past `rtol` |
| `gasification` | `volAverage(CO2)`, `(CO)`, `(O2)` | 2.9e-3, 1.4e-3, 4.5e-4 | step mean |
| `interfaceEnthalpyGasification` | `max(CO)`, `Srho`, `interfaceEnthalpy` | 1.5e-3 | step mean; `T = Ts = 900` and `interfaceEnthalpy/Srho` unchanged |
| `chemistryGateCrossing` | `solidChemistrySh`, `Ywoodm`, `solidSensible` | 5.4e-3, 3.6e-3, 3.5e-3 | both causes below, and the heat source |
| `solidRates` | sources, `heat` | 5.2e-4 at most (`H2`, `H6`, `H7`, `H8s` past `rtol`) | step mean |

- **Step mean.** The rate now reads the ODE state (`6465016`), where it used to read fields frozen at the step start, so a step's source is a mean over the step. The move is first order in dt, and halves when dt does. The heat source followed as an ODE state (`319f33b`), which took `chemistryGateCrossing`'s heat per kg of gas from 0.2% high to exact.
- **Old ODE error.** The old right-hand side did not depend on the concentrations but its Jacobian did, so seulex was accurate only to `relTol`, 1e-4 by default. In `chemistryGateCrossing` the heavy cell's rate ran about 0.5% low whatever dt was, and only the heavy cell. The dt → 0 limit of `pyrolysis` p sat 1.8e-4 below the new build's, and `chemistryGateCrossing`'s `Ywoodm` 3e-3 above. With only those Jacobian entries zeroed, the old build agrees with the new limit to 3e-6. The new build's Jacobian matches its right-hand side and does not depend on `relTol`. `gasification` and `interfaceEnthalpyGasification` do not show this error.

The fixtures were captured with `--all-files`, so `H3`, `H4`, `H5`, `H9s`, `H10s` are recaptured though they were within `rtol`; `H5` and `H9s` by only 2%. Their invariant columns changed with the file. `massRatio` is no longer exactly 1 because the gas rescale that forced it (`1e13ea8`) is gone. `H1` and `H1s` also carry the new time step in the `Time` column.

**#226.** `gasification` only, from build `b5b799f733f4`; `conservation/gasReactionPorosity` gets its first baseline from the same build. YEqn now weights `reaction->R(Yi)` by `porosityF`, as EEqn already weighted `Qdot`. Before, in porous cells the species reacted at 1/porosityF times the rate the heat release implied. `gasification` is the only fast case with gas chemistry in porous cells that actually reacts. `charOnlyMoveCases/serial` has porous cells (0.99), but its gas is air at 352 K or below, so the rate there is zero. `interfaceEnthalpyGasification` runs with combustion `active false`.

| Case | Column | Move | Why |
|---|---|---|---|
| `gasification` | `volAverage(CO2)` | 0.41x at the first sample (≈ porosityF = 0.4), 0.70x at the last | the CO oxidation rate is now 0.4x what it was |
| | `volAverage(CO)`, `(O2)` | +27%, +2.8% at the last sample | less CO is burned |
| | `volAverage(T)`, `(Ts)` | +2.2 K (2.2e-3), +0.1 K (1.0e-4) | CO now depletes more slowly, so more of it is left to release heat later in the run (not analysed further) |

`CharIntegral` and `PorosityAverage` are unchanged.

## Next

- Step 4: turn the invariant rows into explicit checks (bounds, zero residuals, conserved totals, Darcy's law) instead of 100-row copies of one value.
- Replace the dead rows: `Ywoodm` for the wood integral; drop the absent species; weight `Ts` by solid in the char-only cases.
- Settle the questions: radiation reaching the solid (finding 2), the gas energy drift (6), pyrolysis pressure (7), gasification after #174 (8).
- Make the comparator report unreferenced output (finding 10).
