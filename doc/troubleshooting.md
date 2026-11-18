# Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `Cannot find library` | Running outside pgf, or PGF not built for this checkout | `./pgf setup`, `./Allwmake`, then run through `pgf run` / `pgf shell` |
| A script that sources `etc/bashrc` stops silently, with no output | OpenFOAM's `bashrc` fails under `set -u` / `set -e` (and treats the script's positional arguments as settings) | Source it as `set +eu; set --; . etc/bashrc`, or run the command through `./pgf shell -- …` |
| Every case fails with `Error getting 'application' from system/controlDict` | UCX prints `failed to create VFS socket` on stdout, which `RunFunctions` reads as the application name | `export UCX_VFS_ENABLE=n UCX_LOG_LEVEL=fatal` (`pgf` sets both) |
| A DEM run hangs at the Yade/OpenFOAM handshake with `help-mca-bml-r2.txt / unreachable proc` | Only `lo` is up (e.g. a sandbox), and Open MPI excludes it from TCP by default | Run under `pgf shell --yade --mpi loopback` (the `dem` tier does) |
| `mpirun` fails at once with `PMIX_ERR_EXISTS` / `prte_session_dir failed` | A killed `mpirun` left `$TMPDIR/prterun.<host>.<pid>.<uid>`, and this one got the same pid | Remove the stale `prterun.*` dirs; `runDEMCase.sh` gives each run its own `TMPDIR` |
| `utilities/pgf/build.sh` exits with `needs --isolated or --prefix DIR` | The build engine is called directly; there is one build route | `pgf build [--yade]` (or `./Allwmake`) |
| `pgf: refusing wmake` inside `pgf shell` | `wmake` would install into the cached build of the old sources | Run `pgf build` after editing ([Editing and rebuilding](build.md#editing-and-rebuilding)) |
| A deleted or renamed header still appears to be found | Dangling `lnInclude/` symlink from an earlier build | `./Allwclean && ./Allwmake` |
| `undefined symbol` | Library version mismatch or incomplete rebuild | `./Allwclean && ./Allwmake` |
| Simulation crashes immediately | Missing initial fields | Check `solidComponents` match `0/Y*` files |
| Negative temperatures | Too large time step, or aggressive reaction parameters | Reduce `deltaT`, `maxCo`, `maxDi`, or `initialChemicalTimeStep` |
| Porosity exceeds 1 or goes below 0 | Bed collapse too aggressive | Reduce `deltaT` or adjust `solidRegimes/connectedAboveSolidFraction` |
| Mass fractions do not sum to 1 | Missing `Ydefault` field or gas species | Check all gas species have corresponding `0/` files |
| Slow convergence in pressure | Tight PIMPLE settings | Increase `nCorrectors` or relax `p` tolerance |
| Parallel: `decomposePar` fails | Missing decompose constraints | Ensure `Ts`, `porosityF`, `porosityF0` use `calculated` or `zeroGradient` BCs |

## Related pages

- [Setting up your own case](case-setup.md): required fields, input files, tutorial cases, tips.
- [The pgf tool](pgf.md): settings, builds and runs.
- [Building](build.md): the build store, the DEM coupling, cleaning.
- [All documentation](README.md)
