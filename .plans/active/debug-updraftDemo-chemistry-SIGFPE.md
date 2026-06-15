# Plan: Debug updraftDemo SIGFPE in solid chemistry

## Context

The updraftDemo case (`tutorials/cases/updraftDemo/`) crashes with SIGFPE at t≈0.04s in
`ODESolidHeterogeneousChemistryModel::calculateSourceTerms()`. The crash occurs with
both `seulex` (expected — singular Jacobian at cold start) and `RKCK45` (unexpected —
explicit Cash-Karp solver should handle zero derivatives).

The working `charOnlyMove/parallel-recon` case uses different chemistry (char oxidation,
initial Ychar > 0) + `seulex` and runs fine. The updraftDemo uses R24a/b/c primary wood
pyrolysis with NO initial char (Ychar = 0 everywhere).

The DEM coupling is alive — `Us` is solved, particles are spawned, coupling markers
appear in `log.yade`. The crash is purely in the solid chemistry solve path:
`pyrolysisZone.evolve() → solidChemistry_->solve() → solveOneCell() → calculateSourceTerms() → SIGFPE`.

## Files to touch

### Source (for diagnostics only — no fix needed yet)
- `porousGasificationMedia/thermophysicalModels/porousSolidChemistryModel/porousSolidChemistryModel/ODESolidHeterogeneousChemistryModel/ODESolidHeterogeneousChemistryModel.C`
  - Lines 1554-1591: `solveOneCell` — add diagnostic print before `calculateSourceTerms` to log cell index, solidRho, Ti, Ys values
  - Lines 1599-1671: `calculateSourceTerms` — add diagnostic at entry and before `this->solve()` call

### Case files
- `tutorials/cases/updraftDemo/constant/chemistryProperties` — temporary edits for isolation tests
- `tutorials/cases/updraftDemo/log.yade` — crash log (read-only)

## Approach

### Step 1: Isolate chemistry vs DEM
Turn chemistry off temporarily to confirm DEM coupling runs cleanly:

```
chemistry off;
```

Run `./Allrun` on host. If it completes (or runs well past t=0.04s), the bug is chemistry.

### Step 2: Add crash diagnostics
Add a diagnostic print in `solveOneCell()` just before `calculateSourceTerms()`:

```cpp
// In solveOneCell, after line 1584 (initialSpecieConcentration = specieConcentration_)
// and before line 1586 (calculateSourceTerms call):

if (solidRho < SMALL || Ti < 1.0)
{
    Info << "DEBUG solveOneCell celli=" << celli
         << " solidRho=" << solidRho
         << " gasRho=" << gasRho
         << " Ti=" << Ti
         << " porosity=" << porosityF_[celli]
         << " Ywood=" << Ys_[0][celli]
         << " Ychar=" << Ys_[1][celli]
         << endl;
}
```

Also add a print at the START of `calculateSourceTerms`:

```cpp
Info << "DEBUG calculateSourceTerms celli=" << cellCounter_
     << " t0=" << t0 << " deltaT=" << deltaT
     << " Ti=" << Ti << " solidRho=" << solidRho
     << " nSpecie=" << nSpecie_
     << " specieCon[0]=" << initialSpecieConcentration[0]
     << endl;
```

Also wrap the `this->solve()` call at line 1622 in a try-catch (with `FatalIOError`) to
get a clean error with cell index.

**Build**: `build-pgf` (or `cd porousGasificationFoam && wmake`). Then re-run on host
and capture the diagnostic output.

### Step 3: Identify the crashing cell
From the diagnostic output, determine:
- Is it a cell with porosity≈1 (above the bed)? → solidRho ≈ 0, species concentrations ≈ 0
- Is it a cell in the bed with non-zero solid but weird state?
- What are the Ys values (wood, char) in the crashing cell?

### Step 4: Fix based on findings

**Hypothesis A**: Cells with porosity≈1 (above bed) slip through `reactingCells_` filter.
All solid concentrations = 0, but `Ys_[wood][celli]` ≈ 0.7 (initial mass fraction field
not zero). The ODE solver computes tiny forward rates based on Ys_ (non-zero) while
integrating the ODE state `c` (all-zero). The mismatch might cause a NaN in the
temperature update.

**Fix A**: Set `reactingCells_` to false for cells where `porosity > 0.99` or
`solidRho < SMALL`. In `solve()` method (line 1482), before the cell loop, or in
the constructor.

**Hypothesis B**: The `omega()` function at line 652 uses `Ys_[si][cellI]` (mass fraction
field) for rate computation but `c` (ODE state vector) in some branches. The rate
expression `pow(Ys_[si][cellI], n)` with n=1 and Ys_[si] ≈ 0.7 is fine. But with the
ODE state `c[i]` used in `R.kf(T, p, c1)` (line 675), if c1 contains zeros and the
kf computation has a division...

Actually the Arrhenius `kf(T, p, c)` function is just `A * exp(-Ta/T)` for solid
reactions — it ignores `c`. So this is unlikely the issue.

**Hypothesis C**: Floating-point underflow. With `initialChemicalTimeStep 1e-10` and
rates ≈ 1e-20 in cold cells, the ODE state change is ≈ 1e-30. In the temperature
update at line 1660: `dTi = newhi/(newCp*solidRho)*dt_`. If newhi is computed from
omegaPreq[nEqns()] after the solve (which might have accumulated floating-point noise),
the division with near-zero denominator might SIGFPE.

**Fix C**: Add clamping: `dTi = max(min(dTi, 10.0), -10.0);` or guard against near-zero
denominator.

### If diagnostics don't pinpoint:

Try changing `initialChemicalTimeStep` from `1e-10` to `1e-6` in `chemistryProperties`.
Also try setting `solidChemistryTimeStepControl true;`.

## Verification

1. `chemistry off` test: DEM coupling runs past t=0.04s without SIGFPE
2. Diagnostics print: identify which cell and what state triggers the crash
3. Fix applied: case runs cleanly past t=0.5s (at least to first write step)
4. Check log for `Forces+lambdaDot` and `nParticles > 0` at write steps
