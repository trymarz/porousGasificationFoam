# porousGasificationFoam

**An OpenFOAM solver for thermochemical conversion in porous media** — a reacting solid (a porosity field) exchanging mass, momentum and energy with a gas flowing through its pores. Used for biomass pyrolysis, gasification and combustion, wood fires, waste incineration, coffee roasting, peat smouldering and metal-foam manufacturing.

- **Chemistry and geometry are yours.** Gas-phase, gas–solid and solid-decomposition reactions, material properties, initial porosity and kinetics are all per-case inputs; with no reactions defined, both phases stay inert.
- **The solid can move.** It can sit still, fall under gravity and other forces, shrink, or disappear as conversion proceeds.
- **Optional DEM coupling.** [YADE](https://yade-dem.org) can supply the solid velocity field (`--yade` build).

Target: **OpenFOAM-v2406** · License: **GPL v3** · Earlier versions for [OpenFOAM 8](https://github.com/btuznik/porousGasificationFoam) and foam-extend 4.1 are no longer maintained.

## Quick start

You need OpenFOAM-v2406 (installed and sourceable), a C++ compiler and `wmake` (both come with OpenFOAM).

```bash
source /path/to/OpenFOAM-v2406/etc/bashrc   # 1. OpenFOAM environment
./Allwmake                                  # 2. build libraries, solver, utilities
cd tutorials/cases/charOnlyMoveCases/serial # 3. a small serial case
./Allrun                                    #    copies 0.orig to 0, runs blockMesh and
                                            #    setFields, then the solver (output stays
                                            #    in the case directory)
```

Next: `tutorials/cases/gasifier` is a full reactive-flow demo. `./Allwclean` cleans the build. Build options, DEM (`--yade`) builds and clean/purge rules are in [doc/build.md](doc/build.md). To run your own case, start from [doc/case-setup.md](doc/case-setup.md).

## Documentation

| I want to… | Read |
|---|---|
| Build, rebuild, or enable DEM coupling | [doc/build.md](doc/build.md) |
| Set up my own case (input files, initial fields, tips, utilities) | [doc/case-setup.md](doc/case-setup.md) |
| Pick a tutorial to start from | [Tutorial cases](doc/case-setup.md#tutorial-cases) |
| Fix an error | [doc/troubleshooting.md](doc/troubleshooting.md) |
| Understand what the solver does each time step | [doc/solver-tour.md](doc/solver-tour.md) |
| Run or extend the regression tests | [doc/regression-testing.md](doc/regression-testing.md) |
| Contribute (branches, PRs, doc rules) | [CONTRIBUTING.md](CONTRIBUTING.md) |
| Browse the API | Doxygen — see [doc/build.md](doc/build.md#doxygen-documentation) |

## Repository layout

```
porousGasificationFoam/    main solver source (time loop, equations)
porousGasificationMedia/   libraries (pyrolysis, chemistry, radiation, DEM)
tutorials/cases/           tutorial cases
utilities/                 auxiliary tools (setPorosity, totalMass, …)
applications/test/         regression framework
Allwmake / Allwclean       build / clean everything (non-DEM by default)
build.sh                   fine-grained build script
doc/                       documentation and Doxygen source
```

## Citation

If you use this solver, please cite:

> Żuk, P. J., Tużnik, B., Rymarz, T., Kwiatkowski, K., Dudyński, M., Galeazzo, F. C., & Krieger Filho, G. C. (2022). OpenFOAM solver for thermal and chemical conversion in porous media. *Computer Physics Communications*, 278, 108407.

## Contributors

Paweł Jan Żuk, Bartosz Tużnik, Tadeusz Rymarz, Ali Ebrahimi Pure, Kamil Kwiatkowski, Marek Dudyński, Flavio C. C. Galeazzo, Guenther C. Krieger Filho, Filip Mróz (foam-extend-4.1 to v2406 port)

*AI coding agents: see [AGENTS.md](AGENTS.md).*
