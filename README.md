```text
/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | porousGasificationFoam
   \\    /   O peration     | 
    \\  /    A nd           | Advanced multiphase solver for generalized
     \\/     M anipulation  | thermochemical conversion in porous media
-------------------------------------------------------------------------------
 
  [Mass]   ∂(ερ_f)/∂t + ∇·(ρ_f U) = S_m,f|  [S-Mass] ∂((1-ε)ρ_s)/∂t = S_m,s
  [Mom.]   ∂(ερ_f U)/∂t+∇·(ρ_f UU)=-∇p+S |  [Spec.]  ∂(ερ_f Y_i)/∂t+∇·=S_Y,i
  [Energy] ∂(ερ_f h)/∂t+∇·(ρ_f Uh)=S_h,f |  [S-Egy]  ∂((1-ε)ρ_s h_s)/∂t=S_h,s
 
-------------------------------------------------------------------------------
   ► Arbitrary Chemistry Mechanisms      ► Local Thermal Non-Equilibrium
   ► Dynamic Porosity & Permeability     ► Compressible Species Transport
   ► Interphase Heat & Mass Exchange     ► Generalized Source Terms (S_Φ)
\*---------------------------------------------------------------------------*/
```

# porousGasificationFoam

*porousGasificationFoam* (PGF) is an OpenFOAM solver for **thermochemical conversion in porous media**. A solid phase, represented as a porosity field, is coupled to a gas flowing through the void space. Mass, momentum, and energy are exchanged through the porosity. All chemistry — gas-phase, heterogeneous (gas–solid), and solid decomposition — is defined per case. With no reactions defined, both phases stay inert. The solid can be stationary or move under gravity and other external forces, shrink, or disappear entirely as conversion proceeds.

The continuum transport equations are solved with the Finite Volume Method (OpenFOAM). An optional coupling to a Discrete Element Method solver YADE provides PGF with a velocity field for the solid momentum equation. Solid material properties, initial distribution of porosity throughout a domain, reaction kinetics, and many other parameters — all these are user inputs — the solver is geometry- and chemistry-agnostic.

Example application areas:

- Biomass pyrolysis, gasification, and combustion (the solver's origin)
- Wood fires and waste incineration
- Coffee bean roasting
- Peat smouldering
- Metal-foam manufacturing

…and other processes that share a reactive or transforming porous solid exchanging mass and heat with a gas. The name *porousGasificationFoam* reflects the solver's origins in biomass-to-syngas gasification (feedstock + air/O₂/steam/CO₂ → H₂/CO), not a limit on what it can model.

- **License**: GNU GPL v3
- **Target**: OpenFOAM-v2406
- **Optional**: DEM coupling via YADE (`WITH_YADE=1` at build time)
- Earlier versions for [OpenFOAM 8](https://github.com/btuznik/porousGasificationFoam) and foam-extend 4.1 exist but are no longer actively maintained.

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
| Understand the model: equations, per-step flow, where the code is | [doc/theory.md](doc/theory.md) |
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
