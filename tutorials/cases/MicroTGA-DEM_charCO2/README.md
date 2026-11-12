# MicroTGA-DEM_charCO2 — char gasification in CO₂ on the reference DEM case

This case is the reference case
(`../MicroTGA-DEM_lambdaDotApparentVolume/README.md`) with its bed turned into
char that gasifies in CO₂ (#207, Goal 2). The DEM side is the same: the same
`MPI_lambda.py`, mesh, springs, rank counts and time steps. Read the
reference README for those.

## What changes

| | Reference | This case |
|---|---|---|
| Solid | wood → targas, wood → char1 → 0.65 char + 0.35 gas | char only |
| Reaction | pyrolysis | char + CO₂ → 2 CO |
| Gas | N₂ | pure CO₂ (N₂, CO at 0) |
| Ts, T | 650 K, 850 K | 1273 K, 1273 K |
| `diffusionLimitedReactions` | true | false |

The files that are this case's own are `constant/chemistryProperties`,
`constant/reactions`, `constant/thermo.compressibleGas`,
`constant/solidThermophysicalProperties`, `constant/lambdaDict`, `0.orig/`,
`Allrun`, `system/regressionInvariants`, `reference/` and this README.
Everything else is linked to `../MicroTGA-DEM_lambdaDot_common/`.

`reference/postProcessing/` is the regression baseline: the six λ metrics
every DEM fixture carries, plus `lambdaMinusOne*`, which hold the 2e-5 signal
that `lambdaAverage` rounds away.

## The reaction

The coefficients are masses per kg of char, today's convention (#201; see
`doc/case-setup.md`):

```
char + 3.6641 CO2 = 4.6641 CO
(1.778e9 31512 873 1.4362e7 1 0.40)        // A Ta Tcrit heatOfReaction n_char n_CO2
```

Here 3.6641 = 44.0095/12.011 and 4.6641 = 2·28.0101/12.011.

## Where the numbers come from

**Kinetics.** Khalil, Várhegyi, Jäschke, Grønli & Hustad, "CO₂ gasification
of biomass chars: a kinetic study", *Energy & Fuels* 23 (2009) 94–100,
doi:10.1021/ef800739m. An open copy is at
[real.mtak.hu/8894](https://real.mtak.hu/8894/).

- Rate law (their Eq. 4): dα/dt = A·exp(−E/RT)·(1−α)ⁿ·C_CO₂^ν, where
  C_CO₂ is the CO₂ volume fraction. It was fitted to TGA runs over
  600–1000 °C at 51 and 101 kPa CO₂.
- Values: Table 3, pine charcoal, joint evaluation of 14 experiments.
  E = 262 kJ/mol, log₁₀A = 9.25 s⁻¹, n = 0.75, ν = 0.40.
  - Ta = E/R = 262/8.3143e-3 = 31512 K (the paper's R).
  - A = 10^9.25 = 1.778e9 s⁻¹.
  - Tcrit = 873 K, the bottom of the fitted range.
- Why pine: the paper finds that pine's conversion function is close to the
  shrinking-core model, which is how `lambdaMode apparentVolume` treats a
  single-specie particle (below).

**Heat of reaction.** +172.5 kJ/mol, endothermic, as given in the same paper.
Divided by 12.011 g/mol this is +1.4362e7 J/kg of char. It is the standard
value; `heatOfReaction` is meant to be the value at reaction temperature,
and this case has no sourced one.

**Char properties and `rhoApparent`.** `corymbiaChar` in
`thermoData/solidThermoData` (rho 1546, rhoApparent 459 kg/m³, from the
source cited there). Char is the only solid specie, so `rhoApparent` cancels
in r (below), and its value does not change the result.

## How the case departs from the paper

- **Different charcoal.** The kinetics are for pine charcoal, and the char
  properties are for *Corymbia citriodora*. I found no accessible CO₂
  kinetics for eucalyptus char. Tancredi et al., *Fuel* 1996, exists but I
  could not read it.
- **Conversion order.** PGF's rate is A·e^(−Ta/T)·ρ·Y_char^n₁·Y_CO₂^n₂. Char
  is the only solid, so Y_char stays at 1 and the rate per unit cell volume is
  first order in the remaining char, not (1−α)^0.75. Over the 2 ms run
  α ≈ 6e-5, so the difference does not show.
- **CO₂ order.** PGF uses the CO₂ mass fraction; the paper uses the volume
  fraction. They are equal in pure CO₂, which is how the case starts. Once CO
  forms they differ slightly.
- **No film resistance.** `diffusionLimitedReactions` is off: the paper's
  fit is in the kinetic regime (1–2 mg samples).
- **Rate.** At 1273 K, k = A·exp(−Ta/T) ≈ 3.2e-2 s⁻¹. Over 2 ms that gives
  α ≈ 6e-5 and a drop in λ of about 2e-5 (λ³ ∝ remaining mass). The
  endothermic heat cools the solid by about 0.5 K.
- **First run** (PGF `8a0217d`, Foam-Yade `42126e8`, 2 + 2 ranks, about 3 min
  of solver time):
  - per step, r = 1 − 1.04e-8, which is k·dt/3 with k = 3.1e-2 s⁻¹;
  - mean λ ends at 1 − 2.01e-5;
  - mean bed porosity rises by 9.1e-6, i.e. α = 6.0e-5;
  - Ts at the probe falls from 1273 to 1272.48 K.

## What λ does here

With one solid specie, `apparentVolumeLambdaDot` gives
r = cbrt(1 + dV/V) = cbrt(m_new/m_old): the particle shrinks at a constant
apparent density. Yade multiplies each sphere's λ by r once per exchange, and
the springs pull towards `L0·λ̄`. As in the reference case, the sphere radius,
mass and contacts do not follow λ (#206).

## Direction invariant

- `Allrun`: r = `lambdaDot` > 0 in the bed, and the mean λ ends below 1.
- `system/regressionInvariants`: `max(lambdaDot) ≤ 1`, so no cell ever
  grows, and `max(lambda) ≤ 1`, so no particle ever grows (to 1e-12).
  It also keeps the reference case's check that the spring force reaches
  `NewtonIntegrator` (≥ 1e-5 N; the first run gave 1.1e-3 to 1.8e-3 N).

Together these say that λ decreases while char is consumed, and never
increases.
