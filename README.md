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
Gas phase
  ∂(ε ρ_f)/∂t     + ∇·(ρ_f U)     = S_m,f
  ∂(ε ρ_f U)/∂t   + ∇·(ρ_f U U)   = −∇p + S
  ∂(ε ρ_f Y_i)/∂t + ∇·(ρ_f U Y_i) = S_Y,i
  ∂(ε ρ_f h)/∂t   + ∇·(ρ_f U h)   = S_h,f

Solid phase
  ∂((1−ε) ρ_s)/∂t     = S_m,s
  ∂((1−ε) ρ_s h_s)/∂t = S_h,s
```

</div>

*porousGasificationFoam* (PGF) is an OpenFOAM solver for **thermochemical conversion in porous media**. A solid phase, represented as a porosity field, is coupled to a gas flowing through the void space. Mass, momentum, and energy are exchanged through the porosity. All chemistry — gas-phase, heterogeneous (gas–solid), and solid decomposition — is defined per case. With no reactions defined, both phases stay inert. The solid can be stationary or move under gravity and other external forces, shrink, or disappear entirely as conversion proceeds.

The continuum transport equations are solved with the Finite Volume Method (OpenFOAM). An optional coupling to a Discrete Element Method solver YADE provides PGF with a velocity field for the solid momentum equation. Solid material properties, initial distribution of porosity throughout a domain, reaction kinetics, and many other parameters — all these are user inputs — the solver is geometry- and chemistry-agnostic.

Example application areas:

- Biomass pyrolysis, gasification, and combustion (the solver's origin)
- Wood fires and waste incineration
- Coffee bean roasting
- Peat smouldering
- Metal-foam manufacturing

…and other processes that share a reactive or transforming porous solid exchanging mass and heat with a gas. The name *porousGasificationFoam* reflects the solver's origins in biomass-to-syngas gasification (feedstock + air/O₂/steam/CO₂ → H₂/CO), not a limit on what it can model.

Target: **OpenFOAM-v2406** · License: **GPL v3** · Earlier versions for [OpenFOAM 8](https://github.com/btuznik/porousGasificationFoam) and foam-extend 4.1 are no longer maintained.

## Quick start

You need OpenFOAM-v2406 (installed and sourceable), a C++ compiler and `wmake` (both come with OpenFOAM).

```bash
./pgf setup --yes                           # 1. find OpenFOAM, remember it, put pgf on PATH (+ Tab completion)
./Allwmake                                  # 2. build libraries, solver, utilities (pgf build)
cp -rL tutorials/cases/charOnlyMoveCases/serial ~/pgf-serial   # 3. copy a small case out of the checkout
pgf run ~/pgf-serial                        # 4. copies 0.orig to 0, runs blockMesh and setFields,
                                            #    then the solver (output stays in ~/pgf-serial)
```

Run cases from a copy, not inside `tutorials/`: results written there are left in the checkout and picked up by later copies. `-L` copies the files a case shares with its family (symlinks such as `system/paraviewViews.py`); shared material data is found through `$PGF_ROOT`, which pgf sets. If the copy brings output from an earlier run along (`log.*`, `processor*/`, time directories), clean it first with `pgf run ~/pgf-serial Allclean`: `runApplication` skips every step whose `log.*` exists. `pgf run` warns about both.

Next: `tutorials/cases/gasifier` is a full reactive-flow demo. `pgf shell` gives an interactive shell with the build on `PATH`; `./Allwclean` cleans the build. Build options, DEM (`--yade`) builds and clean/purge rules are in [doc/build.md](doc/build.md). To run your own case, start from [doc/case-setup.md](doc/case-setup.md).

## Documentation

| I want to… | Read |
|---|---|
| Build, rebuild, or enable DEM coupling | [doc/build.md](doc/build.md) |
| Set up my own case (input files, initial fields, tips, utilities) | [doc/case-setup.md](doc/case-setup.md) |
| Pick a tutorial to start from | [Tutorial cases](doc/case-setup.md#tutorial-cases) |
| Fix an error | [doc/troubleshooting.md](doc/troubleshooting.md) |
| Understand the model: equations, per-step flow, where the code is | [doc/theory.md](doc/theory.md) |
| Run or extend the regression tests | [doc/regression-testing.md](doc/regression-testing.md) |
| Look at a case in ParaView, or render its views to PNG | [doc/paraview.md](doc/paraview.md) |
| Contribute (branches, PRs, doc rules) | [CONTRIBUTING.md](CONTRIBUTING.md) |
| Browse the API | Doxygen — see [doc/build.md](doc/build.md#doxygen-documentation) |

## Repository layout

```
porousGasificationFoam/    main solver source (time loop, equations)
porousGasificationMedia/   libraries (pyrolysis, chemistry, radiation, DEM)
tutorials/cases/           tutorial cases
utilities/                 auxiliary tools (setPorosity, totalMass, …)
applications/test/         regression framework
utilities/pgf/             pgf's parts: build driver, test tiers, settings, registry
pgf                        the front door: settings, builds, runs, tests (pgf --help)
Allwmake / Allwclean       build / clean everything (non-DEM by default), via pgf build
doc/                       documentation and Doxygen source
```

`pgf tools` explains every tool at the root:

| Tool | What | Why it exists | Use instead when… |
|---|---|---|---|
| `pgf` | settings, isolated builds, runs in a clean environment, test tiers | one front door instead of hand-sourcing OpenFOAM and sharing `$FOAM_USER_APPBIN` | you only want the OpenFOAM-style entry point: `./Allwmake` / `./Allwclean` |
| `Allwmake` | `pgf build --all --no-DEM [args]` | `./Allwmake` is where OpenFOAM users look first | you need options: `pgf build [--yade]`; an in-place build: `utilities/pgf/build.sh` |
| `Allwclean` | `pgf build clean --all [args]`, DEM included | the same convention; cleaning DEM too means no `--yade` state survives a switch back | one target: `pgf build clean [targets]`; add `--purge` to drop the installed build |
| `build.sh` | shim: forwards to `utilities/pgf/build.sh` (deprecated, #274) | old scripts keep working | `pgf build`, or `utilities/pgf/build.sh` in place |
| `pgf-test` | shim: forwards to `pgf test` (deprecated, #274) | old scripts keep working | `pgf test <tier\|all>`, `--status`, `--gc` |

## Citation

If you use this solver, please cite:

> Żuk, P. J., Tużnik, B., Rymarz, T., Kwiatkowski, K., Dudyński, M., Galeazzo, F. C., & Krieger Filho, G. C. (2022). OpenFOAM solver for thermal and chemical conversion in porous media. *Computer Physics Communications*, 278, 108407.

## Contributors

Paweł Jan Żuk, Bartosz Tużnik, Tadeusz Rymarz, Ali Ebrahimi Pure, Kamil Kwiatkowski, Marek Dudyński, Flavio C. C. Galeazzo, Guenther C. Krieger Filho, Filip Mróz (foam-extend-4.1 to v2406 port)

*AI coding agents: see [AGENTS.md](AGENTS.md).*
