# Documentation

New to PGF? Start with the [README](../README.md) quick start, then [build your own case](case-setup.md#your-first-case).

## Using PGF

- [The pgf tool](pgf.md): settings, builds and runs.
- [Setting up your own case](case-setup.md): required fields, input files, tutorial cases, tips, utilities.
- [Viewing a case in ParaView](paraview.md): the case's views, comparing two cases, rendering to PNG.
- [Troubleshooting](troubleshooting.md): symptoms, likely causes and fixes.

## Building and deploying

- [Building](build.md): `pgf build`, where builds land, the DEM coupling, switching variants, cleaning.
- [Apptainer images](containers.md): `pgf pack`, an image of one PGF commit to run on a server.

## Reference

- [pgf reference](pgf-reference.md): every setting, how a build is chosen, the run record, the clean environment, Tab completion, the scripts at the top of the checkout.
- [API reference (Doxygen)](build.md#doxygen-documentation): generated from the source comments.

## The model

- [Theory](theory.md): what the solver models, the equations, the per-time-step flow, where each piece lives in the source.
- [Solid step](solid-step.md): step 4 of that flow, stage by stage.

## Developing PGF

- [Contributing](../CONTRIBUTING.md): branches, issues, PRs, where documentation goes.
- [Regression testing](regression-testing.md): running the tests, adding a case, how the comparison works.
- [Regression metric ledger](regression-ledger.md): what each regression metric can and cannot detect.

Where a page and the source disagree, the source wins.
