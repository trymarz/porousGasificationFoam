<div align="center">

# porousGasificationFoam

**Advanced multiphase solver for generalized thermochemical conversion in porous media**

[![License: GPL v3](https://img.shields.io/badge/license-GPL%20v3-blue)](https://www.gnu.org/licenses/gpl-3.0)
[![OpenFOAM](https://img.shields.io/badge/OpenFOAM-v2406-green)](https://www.openfoam.com)
[![Paper](https://img.shields.io/badge/CPC-10.1016%2Fj.cpc.2022.108407-orange)](https://doi.org/10.1016/j.cpc.2022.108407)

Arbitrary chemistry · Dynamic porosity and permeability · Interphase heat and mass exchange<br>
Local thermal non-equilibrium · Compressible species transport · Generalized source terms

[Quick start](#quick-start) · [Documentation](doc/README.md) · [Tutorials](doc/case-setup.md#tutorial-cases) · [Citation](#citation)

```text
Gas phase  (ε porosity, U superficial velocity)
  ∂(ε ρ)/∂t    + ∇·(ρ U)                      = S_ρ
  ∂(ρ U)/∂t    + ∇·(ρ U U)  − ∇·τ_eff         = −∇p + ρ g − C·U
  ∂(ε ρ Yᵢ)/∂t + ∇·(ρ U Yᵢ) − ∇·(μ_eff ∇Yᵢ)  = ε Rᵢ + S_ρ,ᵢ
  ∂(ε ρ h)/∂t  + ∇·(ρ U h)  − ∇·(α_eff ∇h)   = ε ∂p/∂t + S_h

Solid phase  (Ymₖ mass of solid component k per volume, Uₛ solid velocity)
  ∂Ymₖ/∂t      + ∇·(Uₛ Ymₖ)                   = Rₖ
  ∂Hₛ/∂t       + ∇·(Uₛ Hₛ)  − ∇·(kₛ ∇Tₛ)      = S_h,s
  1 − ε = Σₖ Ymₖ/ρₖ
```

</div>

*porousGasificationFoam* (PGF) is an OpenFOAM solver for **thermochemical conversion in porous media**. A solid phase, represented as a porosity field, is coupled to a gas flowing through the void space. The phases exchange mass through reactions, momentum through drag, and energy through interphase heat transfer; the porosity sets each phase's share of a cell. All chemistry — gas-phase, heterogeneous (gas–solid), and solid decomposition — is defined per case. With no reactions defined, both phases stay inert. The solid can be stationary or move under gravity and other external forces, shrink, or disappear entirely as conversion proceeds.

The continuum transport equations are solved with the Finite Volume Method (OpenFOAM). An optional coupling to a Discrete Element Method solver YADE provides PGF with a velocity field for the solid momentum equation. Solid material properties, initial distribution of porosity throughout a domain, reaction kinetics, and many other parameters — all these are user inputs — the solver is geometry- and chemistry-agnostic.

Example application areas:

- Biomass pyrolysis, gasification, and combustion (the solver's origin)
- Wood fires and waste incineration
- Coffee bean roasting
- Peat smouldering
- Metal-foam manufacturing

…and other processes that share a reactive or transforming porous solid exchanging mass and heat with a gas. The name *porousGasificationFoam* reflects the solver's origins in biomass-to-syngas gasification (feedstock + air/O₂/steam/CO₂ → H₂/CO), not a limit on what it can model.

Target: **OpenFOAM-v2406** · License: **GPL v3** · Earlier versions for [OpenFOAM 8](https://github.com/btuznik/porousGasificationFoam) and [foam-extend 4.1](https://github.com/pjzuk/porousGasificationFoam) are no longer maintained.

## Quick start

You need OpenFOAM-v2406, installed and sourceable.

PGF comes with `pgf`, a command-line tool that finds OpenFOAM, builds the solver, runs and views cases and runs the tests, each in a clean OpenFOAM environment. `pgf help` lists its commands; [doc/build.md](doc/build.md) covers it in full.

```bash
./pgf setup --yes                       # 1. find OpenFOAM (and yade), save where they are, put pgf on PATH
                                        #    (+ Tab completion); --yes takes what it finds without asking
pgf build                               # 2. build libraries, solver, utilities (./Allwmake does the same)
pgf copy charOnlyMoveCases/serial       # 3. copy a small case out of the checkout; prints where it went
pgf run ~/OpenFOAM/$USER-v2406/run/main/charOnlyMoveCases/serial
                                        # 4. copies 0.orig to 0, runs blockMesh, setFields, the solver
pgf view ~/OpenFOAM/$USER-v2406/run/main/charOnlyMoveCases/serial   # 5. look at it in ParaView
```

- **Settings.** Step 1 saves where OpenFOAM lives in `~/.config/pgf/env.conf`. `pgf config` shows the saved values; `pgf help setup` explains the options.
- **Where copies go.** `pgf copy` puts a tutorial in `$PGF_CASE_ROOT/<branch>/<tutorial>`. `PGF_CASE_ROOT` defaults to OpenFOAM's run dir, `$WM_PROJECT_USER_DIR/run` (`~/OpenFOAM/<user>-v2406/run`); `<branch>` is the checkout's branch (here `main`; `/` becomes `-`). `pgf copy --list` lists the tutorials, `pgf status` your copies.
- **What a copy holds.** The whole case, copied with `cp -rL` so the files it shares with its family come along, and a record of the checkout and commit it came from. `pgf run` uses that checkout's build and warns when the branch has moved on.
- **Run copies, not `tutorials/`.** Results written inside the checkout stay there and are picked up by later copies.
- **Rerunning a case.** Clean earlier output (`log.*`, `processor*/`, time directories) first with `pgf run <case> Allclean`: `runApplication` skips every step whose `log.*` exists.

`pgf run` warns about the last two.

### After the first case

- **More tutorials:** [Tutorial cases](doc/case-setup.md#tutorial-cases) describes each one; `pgf copy --list` lists them.
- **A full reactive-flow demo:** `pgf copy gasifier`, a fixed-bed gasifier. It runs for hours on 4 MPI ranks.
- **Work interactively:** `pgf shell` opens a shell with the build on `PATH`.
- **Build options, DEM (`pgf build --yade`), cleaning (`./Allwclean`):** [doc/build.md](doc/build.md).
- **Your own case:** start from [doc/case-setup.md](doc/case-setup.md).

## Documentation

Each topic has its own page; read them one at a time, as you need them.

| Topic | Read |
|---|---|
| Building, rebuilding, DEM coupling | [doc/build.md](doc/build.md) |
| Setting up your own case (input files, initial fields, tips, utilities) | [doc/case-setup.md](doc/case-setup.md) |
| Choosing a tutorial to start from | [Tutorial cases](doc/case-setup.md#tutorial-cases) |
| Troubleshooting errors | [doc/troubleshooting.md](doc/troubleshooting.md) |
| The model: equations, per-step flow, where the code is | [doc/theory.md](doc/theory.md) |
| Regression tests: running and extending them | [doc/regression-testing.md](doc/regression-testing.md) |
| ParaView: looking at a case, rendering views to PNG | [doc/paraview.md](doc/paraview.md) |
| Contributing (branches, PRs, doc rules) | [CONTRIBUTING.md](CONTRIBUTING.md) |
| API reference | Doxygen — see [doc/build.md](doc/build.md#doxygen-documentation) |

## Repository layout

```
porousGasificationFoam/    main solver source (time loop, equations)
porousGasificationMedia/   libraries (pyrolysis, chemistry, radiation, DEM)
tutorials/cases/           tutorial cases
utilities/                 auxiliary tools (setPorosity, totalMass, …)
applications/test/         regression framework
utilities/pgf/             pgf's parts: build driver, test tiers, settings, registry
pgf                        the front door: settings, builds, runs, tests (pgf help)
Allwmake / Allwclean       build / clean everything (non-DEM by default), via pgf build
doc/                       documentation and Doxygen source
```

## Citation

If you use this solver, please cite:

> Żuk, P. J., Tużnik, B., Rymarz, T., Kwiatkowski, K., Dudyński, M., Galeazzo, F. C., & Krieger Filho, G. C. (2022). OpenFOAM solver for thermal and chemical conversion in porous media. *Computer Physics Communications*, 278, 108407.

## Contributors

Paweł Jan Żuk, Bartosz Tużnik, Tadeusz Rymarz, Ali Ebrahimi Pure, Kamil Kwiatkowski, Marek Dudyński, Flavio C. C. Galeazzo, Guenther C. Krieger Filho, Filip Mróz (foam-extend-4.1 to v2406 port)

*AI coding agents: see [AGENTS.md](AGENTS.md).*
