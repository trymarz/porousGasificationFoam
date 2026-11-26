# Documentation

New to PGF? [Install it](install.md), run the [README](../README.md) quick start, then [build your own case](case-guide.md#your-first-case).

Each group answers one question; inside a group, pages run from overview to detail. Where a page and the source disagree, the source wins.

## Installing and building

How do I get PGF onto a machine?

- [Installing](install.md): OpenFOAM v2606, PGF, and YADE from yade-pgf for DEM, on a clean machine.
- [Building](build.md): `pgf build`, where builds land, the DEM coupling, switching variants, cleaning.
- [Apptainer images](containers.md): `pgf pack`, an image of one PGF commit to run on a server.

## Running cases

How do I set up and run a case?

- [Case guide](case-guide.md): your first case, running a case, a DEM case, tips.
- [Tutorials](tutorials.md): every tutorial case, and which tests run it.
- [Case reference](case-reference.md): required fields, solid boundary conditions, every input dictionary.
- [Viewing a case in ParaView](paraview.md): the case's views, comparing two cases, rendering to PNG.

## The pgf tool

What can pgf do, and how does it work?

- [The pgf tool](pgf.md): settings, builds and runs, and how they fit together.
- [pgf reference](pgf-reference.md): every page of `pgf help`, generated.

## The model

What physics does it solve?

- [Theory](theory.md): physics, assumptions, equations.
- [DEM coupling](theory-DEM.md): the idea of the YADE coupling and the body size models.

## How the code implements it

Where does each piece live, and in what order does it run?

- [Code map](code-map.md): where each model lives in the source, and the per-time-step flow.
- [Solid step](solid-step.md): step 4 of that flow, stage by stage.
- [DEM exchange](dem-exchange.md): step 2 of that flow, the exchange with YADE: fields, λ modes, time step, the fixtures' springs.

## Developing PGF

How do I change it safely?

- [Contributing](../CONTRIBUTING.md): branches, issues, PRs, where documentation goes.
- [Regression testing](regression-testing.md): running the tests, adding a case, how the comparison works.
- [Regression metric ledger](regression-ledger.md): what each regression metric can and cannot detect.
- [Developer tools](dev-tools.md): clangd, Serena, and the API reference (Doxygen).
