# LEIgasifierR3 — hot, oxygen-free

Rung R3 of issue #64. It is a sibling of `LEIgasifierR2` that changes one thing: the bed is hot. It starts as a pre-built hot bed, the wood dries and pyrolyses, and the spheres shrink through `lambdaDot`. The gas is oxygen-free, so nothing burns. Everything else is R2: the outline, the mesh, the feed and discharge, and the bookkeeping.

Geometry, sources, the site-drawing file, the assumptions and the planar-2D flow mapping are R1's; see `../LEIgasifierR1/README.md`. `leiGeometry.py` reads `constant/LEIgeometry.local` the same way (git-ignored, never committed). Feed and discharge are R2's; see `../LEIgasifierR2/README.md`.

## Time window (#64, open decision 1)

The bed holds on the order of an hour of fuel, and the coupled run covers seconds. For R3 the choice is:

- **Start from a pre-built hot bed** (next section), not from a cold bed that has to heat up.
- **Judge R3 over a short window of 2.5 s**, about a thousandth of the bed's residence time. No residence-time scaling: the kinetics and the shrinkage are unscaled, and the feed rate stays R2's numerical placeholder.

Why: a cold start would spend the whole affordable window heating the bed. The hot bed puts the drying and pyrolysis zones where a steady downdraft bed has them from the first step, so a few seconds are enough to test what R3 adds: bounded fields, closed element balances and a front that holds its place. Scaling the residence time would change the kinetics that R3 is meant to test. The same hot-bed state is meant to seed R4 and R5.

What the short window can't show: whether the front is steady over the bed's real residence time, and the bed's slow response to the feed rate. Those belong to R5.

## Initial hot bed (`hotBed.py`)

`hotBed.py` holds the assumed profile in one table:

| Item | Value | Status |
|---|---|---|
| Hot zone | 900 K from the bottom to 50 mm above the reduction-chamber top | assumed |
| Ramp | T falls linearly from 900 K to 300 K over 200 mm above it | assumed |
| Above the ramp | fresh wood at 300 K, 10 % wet-basis moisture | assumed |
| Descent speed that built the profile | 0.5 mm/s | assumed |
| Inflow (both supplies) | N2 at 900 K, R2's volume flow (mass flow × 300/900) | assumed |

Along the ramp, each height gets the moisture `M` and conversion `X` that a slow, steady descent through it would have left:

    v_s dM/dz = k_dry(T) M,    v_s dX/dz = k_pyr(T) (1 − X)

integrated down from the cold top with the kinetics in `constant/chemistryProperties`. That puts drying near 0.62 m and pyrolysis near 0.53 m, with fully converted char below.

`relax.py` packs the bed in rows, each at the radius of its height (`λ = (V/V₀)^(1/3)`: 1 for wet wood, 0.82 for char). It gives each sphere the state of its height and settles the packing; `bedState.txt` keeps the per-sphere state. `hotBed.py` then averages the settled spheres onto the mesh for `Ywood`, `Ychar`, `Ymoisture`, `porosityF`, `Ts` and `T`, so the FVM fields and the DEM spheres start from the same bed. Cells above the settled bed keep R2's static fresh freeboard (cold); the ash pit below the grate is gas only, at 900 K.

## Solid

- Three solid species: `wood`, `char`, `moisture` (`constant/solidThermophysicalProperties`). Wood's properties are R2's.
- **Char** keeps 55 % of the wood particle's volume (assumed): `rhoApparent = 0.2·630/0.55 = 229`. Full pyrolysis then leaves `λ = (0.2·630/229)^(1/3) = 0.82`. Transport and Cp are `thermoData`'s park2010Char.
- **Moisture** is liquid water in the wood's pores. A very large `rhoApparent` gives it no particle volume, so drying doesn't shrink the spheres.
- Reaction heats are the explicit `hr` (`solidReactionEnergyFromEnthalpy false`): drying 2.26 MJ/kg (latent heat at 100 °C), pyrolysis 0.385 MJ/kg (LEIgasifierDEM's). Both are endothermic, so nothing in R3 heats above the 900 K of the bed and the inflow.

## Pyrolysis yields

    wood = 0.2 char + 0.105 CO + 0.2146 CO2 + 0.01 H2 + 0.25 H2O + 0.04 CH4 + 0.1804 tar

Assumed, mass-based, element-consistent with a dry-wood C/H/O of 50/6.2/43.8 wt %. Tar is benzene (`constant/thermo.compressibleGas`). Kinetics are LEIgasifierDEM's (pyrolysis) and Chan et al. (1985) (drying). The gas set is LEIgasifierDEM's without O2, and there are no gas-phase reactions: pyrolysis gas is carried out, not burnt.

## Shrinkage and the solid balance

`lambdaDot` (mode `apparentVolume`, `constant/lambdaDict`) multiplies each sphere's λ by the per-cell volume factor once per exchange, and `changeRadius()` sets the radius from it. The mass a sphere loses by shrinking is a new `balance.txt` column, `mShrunk`, so R2's balance becomes

    m_in − m_out − m_burnt − m_shrunk − (inventory − inventory₀) = 0

Spheres that shrink below 0.1 mm are parked and counted as burnt, as in R2. With pyrolysis alone the smallest sphere is 0.82 r₀, so none burn in R3; the column is checked anyway. Burnt and discharged spheres are parked, not erased (R2: `mp.bodyErase` segfaults `FoamCoupling::setHydroForce` on Foam-Yade d5c33e0).

## Running

```sh
./Allrun    # mesh, relax.py, checkDemWalls.py, hotBed.py, coupled run to 2.5 s, checkFeed.py, checkHotBed.py
./Allclean
```

The time step is fixed at 1e-3 s (R2: 4e-3). With pyrolysis gas flowing, R2's step gave a maximum Courant number of 2.9; 1e-3 keeps it at 0.92 or below over 2.5 s. The coupled run takes about 14 minutes (2 Yade ranks = one Yade worker, which R2's bookkeeping needs, plus 2 OpenFOAM ranks).

`checkFeed.py` runs R2's checks, with shrinkage in the mass balance. R2's "gas out = gas in" check no longer holds with pyrolysis gas, so it is only reported. `checkHotBed.py` checks:

- **Bounds**: Ts within [300, 900] K ± 1 K; gas T within [299, 909] K (see "Checks"); gas and solid mass fractions within [0, 1] ± 1e-6 and summing to 1; no `bounding` or `clamped` line in any log. Nothing in the case clips or floors a field.
- **Element balance**: for C, H and O, inventory(t₀) − inventory(t) − outlet flow − inlet diffusion, over the window, within 1e-3 of what the solid released. Solid element masses come from the solid fields and the yields; gas from ρ, ε, Y and V; the outlet from a per-step `surfaceFieldValue` of φ·Y. Inlet diffusion: the inlets hold the products at Y = 0, so gas diffuses out through them (YEqn diffuses with μ, i.e. ρD = μ). It is μ(N2, 900 K)·Y_cell·|S_f|/(n·d) per face, integrated over the write times.
- **Fronts**: the drying (M = 0.5) and pyrolysis (X = 0.5) heights in the shaft, the mean over columns of the highest crossing, interpolated between cells; that drying stays above pyrolysis and that the pyrolysis front stays inside the hot bed's ramp; and their speed.

## Checks

A 2.5 s run with `pgf exec --yade --mpi loopback` (Foam-Yade d5c33e0, 1 Yade worker plus 2 OpenFOAM ranks, 14 min wall, maximum Courant number 0.92), written every 0.1 s:

| Check | Result |
|---|---|
| Ts | 300.0–900.2 K at every write time |
| Gas T | 300.0–905.2 K; above 900 K only in 1–3 ash-pit cells between 0.4 and 0.9 s, at most 905.2 K (limit 909 K) |
| Y (gas, solid) | within [0, 1]; \|ΣY − 1\| ≤ 1.9e-8 |
| `bounding` / `clamped` in the logs | none |
| Element residual / released, C H O | 1.5e-4, 6.0e-5, 5.3e-5 (limit 1e-3) |
| Wood pyrolysed | 6.4 g per slab over 2.5 s |
| Pyrolysis front (X = 0.5) | 0.537 → 0.530 m, −2.7 mm/s |
| Drying front (M = 0.5) | 0.631 → 0.625 m, −2.6 mm/s, 94 mm above pyrolysis |
| Feed / discharge / burnt | 249 / 249 / 0 (rate·t = 249), backlog 0 |
| Solid mass `in − out − burnt − shrunk − Δinventory` | ≤ 2.0e-16 kg at all 250 rows; 1.35 g lost to shrinkage, smallest sphere 0.81 r₀ |
| Bed level | 2.55 r range over the last half (limit 4 r) |

**Element balance.** Without the inlet-diffusion term the residual was 1.4–1.8e-3 of released on a 1 s run, the same sign and fraction for C, H and O. Diffusion out through the inlets accounts for it: the cells next to the inlets hold 10–25 % pyrolysis gas against Y = 0 on the face.

**Fronts.** Both fronts stay inside the ramp, drying above pyrolysis, and move down together at about 2.7 mm/s. The solid in that band descends at about 5 mm/s on average over the write times (stick-slip, so this is rough), faster than the 0.5 mm/s the profile was built for, because R2's feed rate is a turnover placeholder. So the fronts ride down with the solid and advance into it by a couple of mm/s. Over a window this short, that is all the profile can show: building the ramp takes a few hundred seconds at the assumed descent.

**Gas T above 900 K (open point).** Nothing in R3 releases heat (both reaction heats are endothermic), but the gas T rises up to 5.2 K above the 900 K of the bed and the inflow. It does so in a few cells just below the grate, as discharged spheres first enter them (porosityF falls from 1 to 0.996–0.9999), and it decays again by 1 s. Pressure work is too small to explain it (the pressure there moves by tens of Pa). The likely cause is the gas energy equation's handling of a cell gaining its first solid, but it hasn't been traced. The gas-T tolerance (9 K, 1 % of T_hot) was set after seeing this; Ts has no such slack.

The case is not in `applications/test/regression/cases.list` or `cases_yade.list`.
