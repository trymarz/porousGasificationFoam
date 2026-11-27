<div align="center">

# porousGasificationFoam

**Advanced multiphase solver for generalized thermochemical conversion in porous media**

[![License: GPL v3](https://img.shields.io/badge/license-GPL%20v3-blue)](https://www.gnu.org/licenses/gpl-3.0)
[![OpenFOAM](https://img.shields.io/badge/OpenFOAM-v2606-green)](https://www.openfoam.com)
[![Paper](https://img.shields.io/badge/CPC-10.1016%2Fj.cpc.2022.108407-orange)](https://doi.org/10.1016/j.cpc.2022.108407)

Arbitrary chemistry · Dynamic porosity and permeability · Interphase heat and mass exchange<br>
Local thermal non-equilibrium · Compressible species transport · Generalized source terms

</div>

**Gas phase** ($\varepsilon$ porosity, $U_g$ superficial velocity)

$$
\begin{aligned}
{\color{blue}\frac{\partial(\varepsilon\rho_g)}{\partial t}} &+ {\color{orange}\nabla\cdot(\rho_g U_g)} &&= {\color{red}S_\rho} \\
{\color{blue}\frac{\partial(\rho_g U_g)}{\partial t}} &+ {\color{orange}\nabla\cdot(\rho_g U_g U_g)} - {\color{green}\nabla\cdot\tau_{\mathrm{eff}}} &&= {\color{purple}-\nabla p + \rho_g g} - {\color{red}C\cdot U_g} \\
{\color{blue}\frac{\partial(\varepsilon\rho_g Y_{g,i})}{\partial t}} &+ {\color{orange}\nabla\cdot(\rho_g U_g Y_{g,i})} - {\color{green}\nabla\cdot(\mu_{\mathrm{eff}}\nabla Y_{g,i})} &&= {\color{purple}\varepsilon R_i} + {\color{red}S_{\rho,i}} \\
{\color{blue}\frac{\partial(\varepsilon\rho_g h_g)}{\partial t}} &+ {\color{orange}\nabla\cdot(\rho_g U_g h_g)} - {\color{green}\nabla\cdot(\alpha_{\mathrm{eff}}\nabla h_g)} &&= {\color{purple}\varepsilon\frac{\partial p}{\partial t}} + {\color{red}S_h}
\end{aligned}
$$

**Solid phase** ($\mathrm{Ym}_k$ mass of solid component $k$ per volume, $U_s$ solid velocity)

$$
\begin{aligned}
{\color{blue}\frac{\partial\,\mathrm{Ym}_k}{\partial t}} &+ {\color{orange}\nabla\cdot(U_s\,\mathrm{Ym}_k)} &&= {\color{red}R_k} \\
{\color{blue}\frac{\partial H_s}{\partial t}} &+ {\color{orange}\nabla\cdot(U_s H_s)} - {\color{green}\nabla\cdot(k_s\nabla T_s)} &&= {\color{red}S_{h,s}} \\
1-\varepsilon &&&= \sum_k \mathrm{Ym}_k/\rho_k
\end{aligned}
$$

Colours mark the kind of term: ${\color{blue}\text{storage}}$, ${\color{orange}\text{advection}}$, ${\color{green}\text{diffusion}}$, ${\color{red}\text{gas–solid exchange}}$, ${\color{purple}\text{other sources}}$. Short form; the equations as solved are in [Theory](doc/theory.md#equations).

*porousGasificationFoam* (PGF) is an OpenFOAM solver for **thermochemical conversion in porous media**. A solid phase, represented as a porosity field, is coupled to a gas flowing through the void space. The phases exchange mass through reactions, momentum through drag, and energy through interphase heat transfer; the porosity sets each phase's share of a cell. All chemistry — gas-phase, heterogeneous (gas–solid), and solid decomposition — is defined per case. With no reactions defined, both phases stay inert. The solid can be stationary or move under gravity and other external forces, shrink, or disappear entirely as conversion proceeds.

The continuum transport equations are solved with the Finite Volume Method (OpenFOAM). An optional coupling to the Discrete Element Method (DEM) solver, YADE, provides PGF with a velocity field for the solid momentum equation. Solid material properties, the initial porosity distribution, reaction kinetics and many other parameters are user inputs, so the solver is geometry- and chemistry-agnostic.

Example application areas:

- Biomass pyrolysis, gasification, and combustion (the solver's origin)
- Wood fires and waste incineration
- Coffee bean roasting
- Peat smouldering
- Metal-foam manufacturing

…and other processes that share a reactive or transforming porous solid exchanging mass and heat with a gas. The name *porousGasificationFoam* reflects the solver's origins in biomass-to-syngas gasification (feedstock + air/O₂/steam/CO₂ → H₂/CO), not a limit on what it can model.

Target: **OpenFOAM-v2606** · License: **GPL v3** · Earlier versions for [OpenFOAM 8](https://github.com/btuznik/porousGasificationFoam) and [foam-extend 4.1](https://github.com/pjzuk/porousGasificationFoam) are no longer maintained. The OpenFOAM v2406 line is frozen at tag `v2406-final` and branch `maint/of2406`, with the legacy `foamYade` coupler and trymarz/Foam-Yade branch `legacy-2406`.

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

**4. Run it and look at the results** in ParaView. `pgf copy` prints where the copy went; for a checkout on `main` it is:

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

The [table of contents](doc/README.md) lists every page and its sections, from installing PGF to developing it.

## Repository layout

```
porousGasificationFoam/    main solver source (time loop, equations)
porousGasificationMedia/   libraries (pyrolysis, chemistry, radiation, DEM)
tutorials/cases/           tutorial cases
utilities/                 helper scripts (pgf internals, Python tools, compile_commands)
utilities/pgf/             pgf's parts: build driver, test tiers, settings, registry
applications/test/         regression framework, tooling tests, test tiers (tiers.conf)
applications/utilities/    pgfView, the ParaView viewer behind pgf view
thermoData/                solid thermophysical data for cases
submodules/                yade-pgf and PgfDemBridge, for DEM builds
container/                 toolchain container recipe
pgf                        the pgf command: settings, builds, runs, tests (pgf help)
Allwmake                   build everything except DEM, via pgf build
Allwclean                  clean the in-tree build cache of every target; builds stay (pgf gc removes them)
doc/                       documentation and Doxygen source
```

## Citation

If you use this solver, please cite:

> Żuk, P. J., Tużnik, B., Rymarz, T., Kwiatkowski, K., Dudyński, M., Galeazzo, F. C., & Krieger Filho, G. C. (2022). OpenFOAM solver for thermal and chemical conversion in porous media. *Computer Physics Communications*, 278, 108407.

## Contributors

Paweł Jan Żuk, Bartosz Tużnik, Tadeusz Rymarz, Ali Ebrahimi Pure, Kamil Kwiatkowski, Marek Dudyński, Flavio C. C. Galeazzo, Guenther C. Krieger Filho, Filip Mróz (foam-extend-4.1 to v2406 port)

*AI coding agents: see [AGENTS.md](AGENTS.md).*
