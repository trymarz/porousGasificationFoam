<div align="center">

# porousGasificationFoam

**Advanced multiphase solver for generalized thermochemical conversion in porous media**

[![License: GPL v3](https://img.shields.io/badge/license-GPL%20v3-blue)](https://www.gnu.org/licenses/gpl-3.0)
[![OpenFOAM](https://img.shields.io/badge/OpenFOAM-v2606-green)](https://www.openfoam.com)
[![Paper](https://img.shields.io/badge/CPC-10.1016%2Fj.cpc.2022.108407-orange)](https://doi.org/10.1016/j.cpc.2022.108407)

Arbitrary chemistry · Dynamic porosity and permeability · Interphase heat and mass exchange<br>
Local thermal non-equilibrium · Compressible species transport · Generalized source terms

[Quick start](#quick-start) · [Documentation](doc/README.md) · [Tutorials](doc/tutorials.md) · [Citation](#citation)

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

Target: **OpenFOAM-v2606** · License: **GPL v3** · Earlier versions for [OpenFOAM 8](https://github.com/btuznik/porousGasificationFoam) and [foam-extend 4.1](https://github.com/pjzuk/porousGasificationFoam) are no longer maintained.

**On OpenFOAM v2406?** That line is frozen at tag `v2406-final` and branch `maint/of2406`, with the legacy `foamYade` coupler and trymarz/Foam-Yade branch `legacy-2406`.

## Quick start

You need OpenFOAM v2606, installed and sourceable. [doc/install.md](doc/install.md) installs it and PGF on a clean Debian or Ubuntu machine, with the optional DEM coupling; the steps below are its short form.

PGF comes with `pgf`, a command-line tool that finds OpenFOAM, builds the solver, runs and views cases and runs the tests, each in a clean OpenFOAM environment. `pgf help` lists its commands; [doc/pgf.md](doc/pgf.md) covers it in full.

**1. Set up pgf.** In the directory where you cloned PGF, run:

```bash
./pgf setup --yes
```

It looks for your OpenFOAM installation (and yade, needed only for the optional DEM coupling), saves where they are in `~/.config/pgf/env.conf`, and puts `pgf` on your `PATH` with Tab completion. `--yes` takes what it finds without asking. From now on `pgf` works in any directory.

**2. Build the solver.**

```bash
pgf build
```

It builds the libraries and the solver into a build of their own: about 1.5 min the first time, and reused as long as the sources don't change. `./Allwmake` does the same.

**3. Copy a tutorial** out of the repository:

```bash
pgf copy charOnlyMoveCases/serial
```

**4. Run it and look at the results** in ParaView:

```bash
cd ~/OpenFOAM/$USER-v2606/run/main/charOnlyMoveCases/serial
pgf run .
pgf view .
```

`pgf run` copies `0.orig` to `0`, runs `blockMesh`, `setFields` and the solver, and records the run; `pgf view` opens the results in ParaView.

`pgf copy` puts tutorials in OpenFOAM's run directory, under the checkout's branch (`main`), and records which checkout they came from, so `pgf run` uses its build. Run such copies, not the cases in `tutorials/`. To rerun a case, clean it first with `pgf run . Allclean`. `pgf status` lists your copies and settings.

### After the first case

- **More tutorials:** [Tutorials](doc/tutorials.md) describes each one; `pgf copy --list` lists them.
- **A full reactive-flow demo:** `pgf copy gasifier`, a fixed-bed gasifier. It runs for hours on 4 MPI ranks.
- **DEM coupling to YADE:** build YADE from trymarz/yade-pgf at the pinned commit, then `pgf build --yade`, `pgf doctor --yade` and `pgf copy MicroTGA-DEM_lambdaDotOff`; [doc/install.md → DEM coupling](doc/install.md#3-dem-coupling-optional) has every step.
- **Your own case:** start from the [case guide](doc/case-guide.md).

## Documentation

[doc/README.md](doc/README.md) lists every page. In short:

- **Using PGF:** [the pgf tool](doc/pgf.md), [your own case](doc/case-guide.md), [ParaView](doc/paraview.md)
- **Installing, building and deploying:** [installing](doc/install.md), [building](doc/build.md), [Apptainer images](doc/containers.md)
- **Reference:** `pgf help` (every command and setting), [API reference (Doxygen)](doc/build.md#doxygen-documentation)
- **The model:** [theory](doc/theory.md), [solid step](doc/solid-step.md)
- **Developing PGF:** [contributing](CONTRIBUTING.md), [regression testing](doc/regression-testing.md), [regression metric ledger](doc/regression-ledger.md)

## Repository layout

```
porousGasificationFoam/    main solver source (time loop, equations)
porousGasificationMedia/   libraries (pyrolysis, chemistry, radiation, DEM)
tutorials/cases/           tutorial cases
utilities/                 helper scripts (pgf internals, Python tools, compile_commands)
utilities/pgf/             pgf's parts: build driver, test tiers, settings, registry
applications/test/         regression framework
pgf                        the pgf command: settings, builds, runs, tests (pgf help)
Allwmake / Allwclean       build / clean everything (non-DEM by default), via pgf build
doc/                       documentation and Doxygen source
```

## Citation

If you use this solver, please cite:

> Żuk, P. J., Tużnik, B., Rymarz, T., Kwiatkowski, K., Dudyński, M., Galeazzo, F. C., & Krieger Filho, G. C. (2022). OpenFOAM solver for thermal and chemical conversion in porous media. *Computer Physics Communications*, 278, 108407.

## Contributors

Paweł Jan Żuk, Bartosz Tużnik, Tadeusz Rymarz, Ali Ebrahimi Pure, Kamil Kwiatkowski, Marek Dudyński, Flavio C. C. Galeazzo, Guenther C. Krieger Filho, Filip Mróz (foam-extend-4.1 to v2406 port)

*AI coding agents: see [AGENTS.md](AGENTS.md).*
