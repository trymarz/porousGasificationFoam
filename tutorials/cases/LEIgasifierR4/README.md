# LEIgasifierR4 — air-blown ignition

Rung R4 of issue #64. It is a sibling of `LEIgasifierR3` that changes two things: the inert N2 supply becomes air, and the chemistry is complete. A strip of hot char at the primary ports ignites the bed. Pyrolysis gas burns in the gas phase, and char reacts with O2, CO2 and H2O. Everything else is R3: the hot-bed profile, the outline, the mesh, the feed and discharge, the shrinkage and the bookkeeping.

Geometry, sources, the site-drawing file and the planar-2D flow mapping are R1's; see `../LEIgasifierR1/README.md`. `leiGeometry.py` reads `constant/LEIgeometry.local` the same way (git-ignored, never committed). Feed and discharge are R2's, and the hot bed, the solid species and the shrinkage are R3's; see their READMEs.

## Gas supply

Both supplies blow air at 900 K (R3's inflow temperature), at their Table 3 normal flows (Striugas et al. 2014: primary 30, secondary 5 Nm³/h). The flows go through R1's planar-2D map. The air is 21/79 O2/N2 by volume, with ρ_N 1.4276 (O2) and 1.2498 (N2) kg/m³, which gives Y_O2 = 0.2329 and 1.0926e-3 and 1.214e-4 kg/s per slab. The 900 K inflow and the absence of radiation (`radiation off`) are carried over from R3, so neither is calibrated yet.

**Why air, not pure O2.** #64 asked for an O2-blown R4, and the first version blew pure O2 carrying the same O2 mass. It ignited, and its balances closed, but the full run crashed at 0.84 s. O2 that slipped through the bed mixed with CO in the empty ash pit and burnt there with no solid to take the heat. The gas went from 1579 to 4100 K in 30 ms, the Courant number reached 2.5 at the fixed step, the gas density went negative and the solver trapped a floating-point error. Nothing in the model removes that heat: radiation is off and the gas set has no dissociation. The user chose to blow air instead. The O2 mass and the ER stay the same, and the N2 dilutes any gas flame in the pit.

## Feed and ER (#64 R3 open point: feed vs profile)

R3 found that R2's placeholder feed lowers the bed much faster than the 0.5 mm/s used to build the hot-bed profile. With O2 flowing, the feed and the O2 must also agree on an equivalence ratio. R4 sets both from the O2:

- **ER = 0.30 on the slab.** R1's planar map scales each supply by the area ratio at its own diameter, so the 3D ER is not preserved in the slab, and R4 defines ER on the slab itself. Stoichiometric O2 for this wood is 1.386 kg per kg of dry wood (C/H/O 50/6.2/43.8 wt %). At the slab O2 of 2.83e-4 kg/s (the air's), that gives a dry feed of 0.68 g/s.
- **Descent speed** 0.19 mm/s. That is 0.68 g/s divided by the fresh bed's dry wood density (564 kg/m³ of bed) and the shaft's slab section (6.4e-3 m²). `hotBed.py` builds the profile with `v_solid = 1.9e-4`.
- **DEM feed** 1.37 spheres/s, from the same volume rate: the single hex layer holds 7217 spheres per metre of shaft height. The descent in a coupled run is measured (`balance.txt`), not imposed.

The user chose this option over keeping R3's 0.5 mm/s profile, which would need 2.6 times the Table 3 air at ER 0.30.

## Chemistry

**Gas phase.** R4 uses LEIgasifierDEM's set (Ibrahim 2025, Table 5.5), in `constant/reactions`: oxidation of H2, CO, CH4 and tar, the water-gas shift, steam reforming and partial oxidation. The rates are molar, so each reaction is element-consistent. Tar is benzene, which oxidises by C6H6 + 3 O2 = 6 CO + 3 H2. In ESI v2406, gas reactions need `mixture reactingMixture` with `foamChemistryReader` (`constant/thermophysicalProperties`), and R3's `multiComponentMixture` cannot carry them. The thermo file `constant/thermo.compressibleGas` is R3's with O2 added, and the species list now lives in `constant/reactions`.

**Char**, in `constant/chemistryProperties`, mass-based:

| Reaction | Coefficients (kg per kg char) |
|---|---|
| char + O2 → CO | 1 + 1.33203 → 2.33203 |
| char + O2 → CO2 | 1 + 2.66406 → 3.66406 |
| Boudouard | 1 + 3.66406 CO2 → 4.66406 CO |
| water-gas | 1 + 1.49988 H2O → 2.33203 CO + 0.16785 H2 |

The coefficients are exact in the elements, taking char as pure carbon. The inherited 2.66 / 3.66 lose about 1e-3 of the carbon, which is the size of the check's tolerance. The kinetics are those of the gasifier tutorial family; under the #201 convention, A is per kg of solid reactant.

**Diffusion limit.** `diffusionLimitedReactions true` applies the film limit to the char's gaseous agents (Zuk 2022, Eq. 10). `specieTransferProperties` sets the mass-transfer coefficient h = 0.1 m/s, with SAV = 468 m⁻¹ kept from R3.

**Reaction heat.** Heats come from the species enthalpies (`solidReactionEnergyFromEnthalpy true`): Sh = −Σ RR_s h_s(Ts) − Σ RR_g Ha_g(Ts). Total enthalpy is then conserved by construction. The `hr` entries are unused in this mode; they are kept as the 298 K values for reference. Wood's formation enthalpy is −6.0728 MJ/kg, chosen so that pyrolysis keeps R3's 0.385 MJ/kg at 298 K.

## Mass-transfer coefficient h

R3's h = 0.017 m/s came from the gasifier tutorial. R3 never tested it, because it had no O2. In R4 it caps char oxidation below ignition. A 0.6 s smoke run of the pure-O2 version at h = 0.017 cooled the strip from 1200 to 1191 K, and 41 % of the O2 left through the outlet unused.

Wakao–Funazkri for packed beds gives

    Sh = 2 + 1.1 Sc^(1/3) Re_p^0.6,    h = Sh · D / d

For 8 mm pellets (d = 0.008 m), O2 at about 1000 K (D ≈ 1.5e-4 m²/s, Sc ≈ 0.8) and Re_p = 5–13 near the primary ports, that is Sh = 4.7–6.8, so h = 0.09–0.13 m/s. R4 uses 0.1 m/s, uniform. A local h that follows the flow is not in the code.

## Ignition strip (`hotBed.py`)

After the R3 profile is mapped, char cells within 30 mm of a primary port face centre start at 1200 K, both Ts and gas T (`T_ign`, `r_ign`). On the slab mesh that is 8 cells at the 2 primary port faces. `hotBed.py` stops if the strip comes out empty.

## Running

```sh
./Allrun    # mesh, relax.py, checkDemWalls.py, hotBed.py (+ strip), coupled run to 2.5 s, checkFeed.py, checkIgnition.py
./Allclean
```

The time step is 1e-3 s, as in R3. The run uses 2 Yade ranks (one Yade worker, which R2's bookkeeping needs) plus 2 OpenFOAM ranks, on the default `foamYade` coupler. A 1.0 s window takes about 8 min of wall time, so the full 2.5 s should take about 20–25 min.

`checkFeed.py` runs R2/R3's solid bookkeeping. `checkIgnition.py` replaces R3's `checkHotBed.py`:

- **Bounds.** Ts and gas T must be at least T_cold − 1 K and no higher than the lowest JANAF Thigh. Mass fractions must lie in [0, 1] ± 1e-6 and sum to 1. No log may contain a `bounding` or `clamped` line. Nothing in the case clips or floors a field.
- **Element balance (C, H, O).** As in R3, plus the O2 inflow and the char burnt off. The inflow, outflow and inlet-diffusion terms come from per-step `surfaceFieldValue` integrals at the outlet and both inlets (`outletSpecies`, `primarySpecies`, `secondarySpecies`). The residual must be within 1e-3 of what the solid released plus the inflow.
- **Energy balance.** The total enthalpy is H = Σ V [ερ Σ Y_i Ha_i(T) + Σ Y_m h_m(Ts)], with JANAF read from the thermo file. Its change must equal the port enthalpy flows, plus inlet conduction and diffusion, plus pressure work Σ V ε Δp. The residual must be within 1e-2 of the formation enthalpy moved. With enthalpy-mode heats, this needs no reaction-rate integral.
- **O2 accounting.** In, out (slip), held in the pores and consumed, reported.
- **Reaction zone.** From 0.5 s on, the hottest char cell above the grate must be hotter than `T_ign` and within 60 mm of a primary port face.
- **Fronts.** Drying and pyrolysis heights above the ramp's foot are reported. The check fails only if drying ends up below pyrolysis.

## Checks

**1.0 s check, air** with `pgf exec --yade --mpi loopback` (Foam-Yade d5c33e0, 1 Yade worker plus 2 OpenFOAM ranks, 8 min 11 s wall, maximum Courant number 0.61), written every 0.1 s:

| Check | Result |
|---|---|
| Ts | 300.0–1220.9 K |
| Gas T | 300.0–1191.1 K |
| Y (gas, solid) | within [0, 1]; \|ΣY − 1\| ≤ 1.7e-8 |
| `bounding` / `clamped` in the logs | none |
| Element residual / (released + in), C H O | 3.4e-4, 3.9e-4, 1.5e-4 (limit 1e-3) |
| Energy residual / formation enthalpy moved | 3.5e-4 (limit 1e-2) |
| O2 | 8.5 % slip, 86 % consumed |
| Reaction zone | hottest char 1200 → 1221 K, rising about 2 K per 0.1 s, 29 mm from a primary port |
| Ash-pit gas T | 900 K up to 0.5 s, then 906, 913, 922, 936, 948 K at 0.6 … 1.0 s |
| Fronts above the ramp's foot | pyrolysis 63 mm, drying 154 mm, unchanged |
| Feed bookkeeping (`checkFeed.py`) | OK |

It runs past the pure-O2 crash time (0.84 s). The char at the ports heats much more slowly than with pure O2 (1217 K against 1321 K at 0.8 s). The gas there stays below the strip's 1200 K, because the N2 carries heat away.

**Ash pit (open point 0).** The hottest pit cell sits just below the grate. At 1.0 s it holds Y_O2 0.076 and Y_CO 0.24 at 948 K, and the pit as a whole holds up to Y_O2 0.20 and Y_CO 0.45. Its temperature rise is speeding up. This is the mixture that ignited with pure O2, now diluted by N2. Burning all of its O2 would heat it by roughly 1000 K, against about 3000 K with pure O2. Whether it ignites within the 2.5 s window, and whether the run survives that at the fixed 1e-3 s step, is not known yet.

**Pure-O2 version (superseded).** A 0.6 s smoke run passed every check: the char reached 1277 K, O2 slip was 5.4 %, element residuals were ≤ 2.9e-4 and the energy residual 4.8e-4. The full run crashed at 0.84 s on ash-pit gas ignition, as described under "Gas supply". The shaft descent measured in that run was a median sphere velocity of 0.18–0.19 mm/s from 0.5 to 0.8 s, against 0.19 mm/s set.

## Open points

0. **Ash-pit gas.** O2 that slips through the bed mixes with CO in the empty pit, and the pit gas is heating (see "Checks"). With pure O2 it ignited and crashed the run. With air it has not ignited by 1.0 s. Radiation is off and the gas set has no dissociation, so nothing in the model limits a pit flame.
1. **Gas T overshoot in the ash pit (R3).** It is still untraced. Now that reactions release heat, it cannot be isolated in R4, so R4 has no gas-T slack above the JANAF range.
2. **Feed vs profile (R3).** Settled for R4 at ER 0.30 on the slab. The descent measured in the pure-O2 run agrees with 0.19 mm/s.
3. **Char yield and char `rhoApparent`** are still assumed (R3).
4. **Spheres are parked, not erased** (`mp.bodyErase` segfaults on Foam-Yade d5c33e0; R2).
5. **One Yade worker only** (R2's bookkeeping).
6. **h is uniform.** It is 0.1 m/s from Wakao–Funazkri at the ports' Re_p; away from the ports the flow is slower and the true h is lower.
7. **Inflow at 900 K, radiation off**, both carried over from R3.
8. **Gas chemistry.** It is LEIgasifierDEM's set as published, not re-validated for this case.
9. **#64 asked for O2-blown.** R4 blows air (see "Gas supply"). An O2-blown rung would need the pit flame resolved first.

The case is not in `applications/test/regression/cases.list` or `cases_yade.list`.
