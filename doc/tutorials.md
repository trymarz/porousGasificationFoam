# Tutorials

The cases under `tutorials/cases/`, one row per case or family. `pgf copy --list` lists them, `pgf copy <case>` copies one out of the checkout, and `pgf run` runs the copy ([case guide → your first case](case-guide.md#your-first-case)). A case with its own README is the home of what that case shows; the row links to it.

"Tested" says which test tier runs a case as a regression test ([regression testing](regression-testing.md)): `fast` and `convergence` come from `applications/test/regression/cases.list`, `dem` from `cases_yade.list`. An untested case still runs, but nothing checks its results.

"Run time" is the wall time of one `pgf run` of the copy, with the ranks the case's `Allrun` uses; a blank means it has not been measured yet.

| Case | What it is | Tested | Run time | Notes |
|---|---|---|---|---|
| `canonical/*` | Small single-feature cases (pyrolysis, gasification, radiation, enthalpy, Darcy, Forchheimer, …) | fast | | Regression fixtures |
| `charOnlyMoveCases/` | Moving char, no reactions; solid inlets, boundary policy, start-up checks | fast, convergence (`serial_m2`, `serial_m4`) | | `serial` is the small fast one; `parallel` is 4-proc |
| `prescribedUsCases/*` | Prescribed solid velocity: converging, diverging, shear, vortex, compaction, … | fast | | Solid-transport tests |
| `MicroTGA-DEM_lambdaDot*` | DEM-coupled micro TGA, one per `lambdaDot` model | dem | | Need a `--yade` build. [`...ApparentVolume`](../tutorials/cases/MicroTGA-DEM_lambdaDotApparentVolume/README.md) is the template for a new DEM case |
| [`MicroTGA-DEM_charCO2/`](../tutorials/cases/MicroTGA-DEM_charCO2/README.md) | The DEM template with its bed turned into char gasifying in CO₂ | dem | | Needs a `--yade` build |
| `shrinkingBedDEM/` | A 2-D column with one reactive layer under `apparentVolume`: the spheres shrink and the bed above settles | no | | Needs a `--yade` build |
| `microTGA/` | Micro-scale TGA, MeanTemp and P1 variants | no | | Radiation on; `Allrun` makes both variants from `microTGATemplate/` |
| `macroTGA_688K/`, `macroTGA_879K/` | Macro-scale TGA at 688 K / 879 K | no | | `*_fine/` = refined mesh |
| `macroTGA_experimentalData/` | TGA with experimental data comparison | no | | |
| `gasifier/` | Fixed-bed gasifier | no | | Full reactive-flow demo |
| `flatPlate/` | Flat plate reactive flow | no | | |
| `biomassPressureDrop/` | Pressure drop through a biomass bed | no | | |
| `solidAdvection/` | Solid advection | no | | |

- `MicroTGA-DEM_lambdaDot_common/` and `DEM_common/` are shared input for the DEM cases, not runnable cases.
- `tutorials/experimental/` holds DEM cases that are kept but not tested or supported: `simple_line_case/` (a DEM-coupled 1-D packed line, pyrolysing) and `MicroTGA-DEM_Us_UsInterp/` (a long `UsInterp` demonstration run). Both need a `--yade` build.
- `tutorials/CleanAllCases.sh` runs every case's `Allclean` (nested cases included) and exits non-zero if any fails.
- foamcli is a separate, optional tool that PGF does not ship or need. Cases with a `.foamcli-monitor.toml` save PNG snapshots under `<case>/render/` when run under it; `foamcli gallery cases`, run in `tutorials/`, writes `tutorials/cases/foamcli-gallery.html`, one searchable page over all of them.

## Case families

Cases of one family share files the way they share dicts: the MicroTGA-DEM lambdaDot fixtures symlink theirs (`system/paraviewViews.py` among them) from `MicroTGA-DEM_lambdaDot_common`, and `charOnlyMoveCases`, `canonical`, `canonical/conservation` and `prescribedUsCases` keep one `paraviewViews.py` in the family directory that each case links as `system/paraviewViews.py` (the regression runner's run copy links the family directory's files too). Copy such a case with `pgf copy`, which copies the linked files instead of links that dangle outside the family. Helper scripts a family shares (`canonical/solveForE`, `canonical/solidRates/solidRates.py`, `solidBoundaryPolicy/copyBase`) are called through `${PGF_ROOT:-../..}`, so a copy finds them in the checkout; `pgf copy` refuses a case whose `Allrun` or `Allclean` still reaches a bare `../`.

## Related pages

- [Case guide](case-guide.md): setting up your own case from one of these.
- [Case reference](case-reference.md): every field and dictionary.
- [Regression testing](regression-testing.md): the test tiers that run them.
- [All documentation](README.md)
