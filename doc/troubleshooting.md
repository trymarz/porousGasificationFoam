# Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `Cannot find library` | OpenFOAM environment not sourced, or PGF not built into this `$WM_PROJECT_USER_DIR` | `source /path/to/OpenFOAM-v2406/etc/bashrc`, then `./Allwmake` |
| `Build variant mismatch` | Switching between the normal and `--yade` builds without cleaning | `./build.sh clean --all --purge`, then rebuild in the wanted mode |
| A deleted or renamed header still appears to be found | Dangling `lnInclude/` symlink from an earlier build | `./Allwclean && ./Allwmake` |
| `undefined symbol` | Library version mismatch or incomplete rebuild | `./build.sh clean --all && ./build.sh build --all` |
| Simulation crashes immediately | Missing initial fields | Check `solidComponents` match `0/Y*` files |
| Negative temperatures | Too large time step, or aggressive reaction parameters | Reduce `deltaT`, `maxCo`, `maxDi`, or `initialChemicalTimeStep` |
| Porosity exceeds 1 or goes below 0 | Bed collapse too aggressive | Reduce `deltaT` or adjust `criticalPorosity` |
| Mass fractions do not sum to 1 | Missing `Ydefault` field or gas species | Check all gas species have corresponding `0/` files |
| Slow convergence in pressure | Tight PIMPLE settings | Increase `nCorrectors` or relax `p` tolerance |
| Parallel: `decomposePar` fails | Missing decompose constraints | Ensure `Ts`, `porosityF`, `porosityF0` use `calculated` or `zeroGradient` BCs |
