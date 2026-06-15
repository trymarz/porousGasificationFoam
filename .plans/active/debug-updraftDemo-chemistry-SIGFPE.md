# Plan: Debug updraftDemo SIGFPE in solid chemistry

**Status: DONE (Bug #1). Fix applied & VERIFIED on host — the guard fired exactly as
predicted (cell 75, porosity 0.996, newCp=0, newhi=8.9e12, Ywood=-4.6e23). The run then hit a
SECOND, deeper crash in `volPyrolysis::heatTransfer()` caused by a solid-advection blowup
upstream — tracked in the follow-on plan `fix-updraftDemo-solid-advection-blowup.md`.**

> The host log revealed the `Ywood=-4.6e23` in this guard's own diagnostic — proof the field
> was already garbage *before* chemistry. Chemistry is a victim, not the cause. The chemistry
> guard here is correct and should stay (defensive); the real disease is fixed in the
> follow-on plan. Move this file to `archive/` once that lands.

*Last updated: 2026-06-15 (Claude debug session #2). Code edited in `/workspace`, not committed.*

---

## TL;DR

- **Crash site:** `ODESolidHeterogeneousChemistryModel::calculateSourceTerms`, line 1660 — the
  post-solve **solid-temperature update** `dTi = newhi/(newCp*solidRho)*dt_`.
- **Why:** denominator `newCp*solidRho ∝ (1−porosity)²` vanishes faster than the numerator
  `newhi ∝ (1−porosity)` for a near-empty-but-still-reacting cell ⇒ `dTi → ∞` ⇒ inf/NaN ⇒
  SIGFPE under `FOAM_SIGFPE`.
- **Smoking gun:** the ODE right-hand side `derivatives()` (lines 788–790) computes the *same*
  ratio but **clamps it** (`min(500, |dTdt|)`). The post-solve update was **not** clamped.
  That asymmetry is exactly why the fault lands in `calculateSourceTerms` and never in the
  ODE solver — and why both `seulex` and `RKCK45` crash identically.
- **Fix applied:** guard the vanishing denominator + mirror the `derivatives()` limiter at
  line 1660. Skip the update when there is effectively no solid (physically correct).
- **Verification done:** full PGF build in-container succeeds, no errors (built into
  `/home/agent`, NOT the host binary). **Still need:** host rebuild `--yade` + `./Allrun`.

---

## Context

The updraftDemo case (`tutorials/cases/updraftDemo/`) crashed with SIGFPE at t≈0.0411 s in
`ODESolidHeterogeneousChemistryModel::calculateSourceTerms()`, on MPI rank 1. The crash
occurred with both `seulex` and `RKCK45`, which was the puzzle: an explicit Cash-Karp solver
should not care about a singular Jacobian. The reason both crash the same way is that the
fault is **not in the ODE integration at all** — see root cause below.

The working `charOnlyMove/parallel-recon` case uses char-oxidation chemistry (initial
Ychar > 0) + `seulex` and runs fine. updraftDemo uses R24a/b/c primary wood pyrolysis with
**no initial char** (Ychar = 0 everywhere; Ywood = 1 in the bed, 0 above it).

The DEM coupling is alive — `Us` is solved, particles are spawned, coupling markers appear in
`log.yade`. The crash is purely in the solid-chemistry solve path:
`pyrolysisZone.evolve() → solidChemistry_->solve() → solveOneCell() → calculateSourceTerms() → SIGFPE`.

## Root cause (traced through the source)

The backtrace (`log.yade`, ~line 8643) shows `sigFpe::sigHandler` → libc → **`calculateSourceTerms`**
→ `solveOneCell` → `solve`. There is **no `seulex`/`RKCK45`/`derivatives` frame** between the
signal handler and `calculateSourceTerms`, so the FPE is raised by `calculateSourceTerms`'s own
arithmetic, *after* `this->solve()` returns — i.e. in the temperature-update block (lines
1625–1668), not inside the ODE step.

The only divisions in that block are:
- `/dt_` (line 1636) — `dt_` is `max(dt_, SMALL)` / `min(deltaT, deltaTChem_)`, never 0. Not it.
- `/rho(Ti)` (lines 1646/1655) — `constRho`, constant nonzero. Not it.
- **`newhi/(newCp*solidRho)` (line 1660) — the culprit.**

Chain of reasoning:

1. **Reacting-cell gate is too permissive.** `volPyrolysis::preEvolveRegion`
   (`porousGasificationMedia/pyrolysisModels/pyrolysisModel/volPyrolysis/volPyrolysis.C:1188`)
   sets `reactingCells_` from `whereIs_`, and `whereIs_[cellI] = 1` whenever
   `porosity_[cellI] < 1.` (line 1049) — *any* solid fraction, however tiny, qualifies.

2. In `calculateSourceTerms`:
   - `solidRho = rho·(1−porosity)` → tiny as porosity→1 (`rho` is the *true* solid density,
     `solidThermo().rho()`, a `constRho` ≈ const, never 0 — so `solidRho=0` only at
     porosity *exactly* 1, where `newhi=0` too and the old `newhi==0` guard saves it).
   - `specieConcentration_[i] = solidRho·Ys_[i]` → tiny; `newCp = Σ specieConcentration·Cp ∝ solidRho`.
   - denominator `newCp·solidRho ∝ (1−porosity)²`.
   - `newhi = omegaPreq[nEqns()]`: the reaction heat is computed in `omega()` from the
     **`Ys_` mass-fraction field** (`omega(...)` scalar overload, line 685 — `pow(Ys_[si][cellI], n)`)
     and the **full solid density** (line 700, `kf *= solidThermo().rho()`), scaled only by
     `(1−porosity)`. So `newhi ∝ (1−porosity)` — it vanishes only *linearly*.
   - ⇒ `dTi = newhi/(newCp·solidRho)·dt_ ∝ (1−porosity)/(1−porosity)² = 1/(1−porosity)` →
     blows up as porosity→1⁻ → inf/NaN → **SIGFPE**.

3. **The asymmetry that proves it.** `derivatives()` (the ODE RHS) computes
   `dTdt = newhi/newCp` (line 788) but then **limits** it (lines 789–790):
   `dtMag = min(500, |dTdt|); dcdt[nSpecie_] = dTdt*dtMag/(|dTdt|+1e-10)`. So during the
   solve the temperature derivative is bounded to ±500 and never overflows — which is why the
   ODE solver itself never faults. The post-solve update at 1660 had no equivalent limiter.

**Why charOnlyMove is immune:** its bed/gas cells don't reach the near-empty-yet-reacting
state. updraftDemo gets there at t≈0.04 once DEM `Us` motion + gas heating drive a cell's
porosity toward 1 while its `Ywood` mass-fraction is still finite.

This is the plan's original Hypothesis A (porosity≈1 cells slip the filter) **combined with**
the Hypothesis C location (the temperature-update division). Hypothesis B (rate uses `c` with
a division) was checked and rejected — the Arrhenius `kf` ignores `c`.

## Fix applied

File: `porousGasificationMedia/.../ODESolidHeterogeneousChemistryModel/ODESolidHeterogeneousChemistryModel.C`,
line 1660 (the `scalar dTi = ...` statement). Replaced the single ternary with:

- a guard: skip the update (`dTi = 0`) when `newhi == 0` **or** `newCp*solidRho <= VSMALL`
  (no solid mass ⇒ no solid temperature to advance — physically correct);
- a limiter mirroring `derivatives()`: `dTi = max(min(dTi, 500*dt_), -500*dt_)`;
- a **one-shot `Pout` diagnostic** that fires when the guard trips, printing
  `celli, porosity, solidRho, newCp, newhi, Ti, Ywood, Ychar` — so the host run both verifies
  the fix and confirms the offending cell state. (Marked "remove once validated".)

No other files touched. `derivatives()` is already protected, so it was left alone (minimal scope).

**Alternative considered (not taken):** tighten the `porosity < 1` criterion in
`volPyrolysis.C:1049` to exclude near-empty cells. Rejected as higher-risk — `whereIs_` also
drives solid transport and bed-surface detection, so changing it has wider numerical
consequences than the localized division guard.

## Verification status

- [x] Code compiles: full `build-pgf` in-container succeeds, no errors. **NOTE:** this builds
      into `/home/agent/OpenFOAM/user-v2406` (container), **not** the host binary at
      `/home/tr/OpenFOAM/tr-v2406`. Compilation is validated; runtime is not.
- [ ] **Host rebuild + run (REQUIRED — needs Yade, absent in container):**
      ```bash
      ./build.sh build --yade            # in the repo root on host
      cd tutorials/cases/updraftDemo && ./Allrun
      ```
- [ ] Check `log.yade`: passes t≈0.0411 s, reaches a write step / t≈0.5 s, no SIGFPE.
- [ ] Check for `skipped solid T update` lines (the diagnostic) — confirms the mechanism and
      reports the cell state.
- [ ] Decide: keep diagnostic or strip it; decide whether to also clamp/guard is enough or
      whether the reacting-cell filter should additionally be tightened.
- [ ] Confirm DEM still alive at write steps (`Forces+lambdaDot`, `nParticles > 0`).

updraftDemo is **not** in `applications/test/regression/cases.list`, so no committed baseline
is invalidated by this change.

## Notes / open questions for after the host run

- Is the FPE an exact divide-by-zero or an overflow-to-inf? The diagnostic's `solidRho`/`newCp`
  values will tell us. Either way the guard+clamp covers it.
- If many cells trip the guard every step, that hints the real issue is upstream (cells that
  should not be reacting at all) → revisit the `volPyrolysis.C:1049` filter as a follow-up.
