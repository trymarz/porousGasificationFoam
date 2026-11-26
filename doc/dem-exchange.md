# DEM exchange

Once per PGF time step, YADE sends PGF where its DEM bodies are and how fast they move, and PGF sends back how each body's λ changes. This page follows that exchange, step 2 of the [per-time-step flow](theory.md#per-time-step-flow). Why the coupling works this way: [DEM coupling](theory-DEM.md). **If this page and the code differ, the code wins.**

## Running a coupled case

A coupled case needs a `WITH_YADE` build ([build.md](build.md#yade-dem-coupling)) and `active true` in `constant/yadeProperties`. `pgfDemRun` (`tutorials/cases/DEM_common/`) starts YADE and the solver together in one `mpirun`; the solver always runs with `-parallel`.

The solver's `system/controlDict` alone sets the run's time. The YADE script reads `deltaT` and `writeInterval` from it through `tutorials/cases/DEM_common/foamTime.py` and sets no step count of its own: the solver's last step ends YADE.

## One exchange, step by step

1. **Bodies go up.** YADE sends each body's position, velocity and λ to every OpenFOAM rank whose mesh bounding box holds the body's centre.
2. **Bodies land on the mesh.** Each rank finds the cell holding each body and averages the bodies per cell into `UsDEM` and `lambdaDEM`. These are smoothed over the solid region into `Us` and `lambda` ([`constant/lambdaDict`](case-setup.md#constantlambdadict-required-by-with_yade-builds-running-a-dem-case)). `Us` then moves the solid in this step's solid transport; `lambda` is output only, since YADE owns each body's λ.
3. **λ values go down.** Each rank returns one value per body: the rate `λ̇` or the factor `r`, depending on the [body size model](theory-DEM.md#body-size-models). It is the value the solid step assembled in the previous time step, because this step's solid step has not run yet. Cells that are almost all gas (`porosityF ≥ connectedBelowPorosity`) send the neutral value, 0 for a rate and 1 for `r`.
4. **YADE applies them.** A body near a rank boundary may be sent to several ranks. Only the ranks that found it in their cells answer, and YADE takes the lowest such rank's value ([#229](https://github.com/trymarz/porousGasificationFoam/issues/229)). A body no rank found keeps its previous value and is counted in `PgfCoupling.nLost`. A rate is then integrated by YADE every DEM step; `r` multiplies λ once, here.
5. **YADE runs to the next exchange.** The solver tells YADE its `deltaT` and whether this was its last step. YADE moves the bodies until the next exchange.

## Keeping time

YADE runs several DEM steps per PGF step, set by `O.dt = deltaT / DEM_SUBSTEPS` with a whole number of substeps.

- **Rate modes** (`constant`, `exactDifferential`). YADE integrates `λ̇` every DEM step, so λ advances by `λ̇ · deltaT` per exchange only when the DEM steps fill the PGF step exactly. The run stops unless YADE's `dt` divides `deltaT` to within 10⁻⁶. Before this check, the fixtures ran with a DEM step 5 times the PGF step, so λ advanced 5 times too fast ([#230](https://github.com/trymarz/porousGasificationFoam/issues/230)).
- **`apparentVolume`.** `r` is applied once per exchange, so the ratio of the steps does not change λ, and no exact ratio is needed.

## Start and stop

At startup the two sides compare the protocol version, the buffer layout, the MPI thread level and `lambdaMode`, and both stop with a message naming the mismatch. They also stop if the OpenFOAM ranks disagree on `lambdaMode`, or if `PgfCoupling.expectLambdaMode` in the YADE script differs from the case's. A YADE script without the `PgfCoupling` engine leaves the solver waiting at startup.

The solver's last step ends YADE, with `PgfCoupling: OpenFOAM ran its last step` in the log. A YADE run that ends first stops the solver, and an MPI error on either side aborts both.

## The fixtures' spring model

YADE only stores and updates λ; nothing in YADE or yade-pgf reads it ([#206](https://github.com/trymarz/porousGasificationFoam/issues/206)). In the DEM tutorial fixtures λ gets a mechanical effect through springs, written in Python in their shared script `tutorials/cases/MicroTGA-DEM_lambdaDot_common/MPI_lambda.py`.

Each sphere is joined to its 6 nearest neighbours. A spring's rest length is its initial length times the mean λ of its two ends, so a growing λ pushes the spheres apart and a shrinking one pulls them together. The force is linear in the stretch; very compressed springs go slack and very stretched ones break. Parameters: [the reference DEM-spring case](../tutorials/cases/MicroTGA-DEM_lambdaDotApparentVolume/README.md).

The springs must add their forces after `PgfCoupling` and before `NewtonIntegrator` in the engine list: YADE resets forces every step, so a force added after `NewtonIntegrator` is lost ([#231](https://github.com/trymarz/porousGasificationFoam/issues/231)). The `springForceAtNewton` output records the spring force `NewtonIntegrator` actually integrates, and the fixtures assert it is positive.

## Where it lives in the code

| Piece | Location |
|---|---|
| PGF side of the exchange | `porousGasificationMedia/DEM/pgfDemCoupler/` |
| Bodies to `Us` and `lambda`, gating | `porousGasificationMedia/DEM/lambdaDotModel.C` |
| The body size models | `porousGasificationMedia/DEM/lambdaDotModels/` |
| YADE side | `PgfCoupling` engine, `pkg/openfoam/PgfCoupling.cpp` in [yade-pgf](https://github.com/trymarz/yade-pgf) (`submodules/yade-pgf`) |
| λ integration in YADE | `NewtonIntegrator` |
| Shared protocol | [PgfDemBridge](https://github.com/trymarz/PgfDemBridge) (`submodules/PgfDemBridge`), described in `src/PgfDemBridge.hpp` |

Both sides build the same protocol core, and neither includes the other's headers.

## Limitations

- **Cross-rank springs.** Each YADE rank computes every spring whose two bodies it can see, and remote copies of bodies carry a stale λ. With 2 or more YADE workers, a spring across ranks is counted 0, 1 or 2 times ([#231](https://github.com/trymarz/porousGasificationFoam/issues/231)). The fixtures run one YADE worker, so it does not occur there.
- **Always decomposed.** Even one OpenFOAM rank needs a one-piece `processor0`; the fixtures use 2.
- Limitations of the coupling itself: [DEM coupling → Limitations](theory-DEM.md#limitations).

## Related pages

- [DEM coupling](theory-DEM.md): the idea and the body size models.
- [Theory](theory.md): the gas–solid model and the per-time-step flow.
- [All documentation](README.md)
