/*---------------------------------------------------------------------------*\
Global
    createYadeCoupling

Description
    Instantiate the YADE coupling object and the per-cell lambdaDot
    updater. Only included when \c WITH_YADE is defined and only
    executed when the runtime switch \c DEM is true.

    \c FoamYade is the C++ side of the YADE coupling library. It owns
    references to the gas fields used by the drag closure, plus the
    sink fields it writes back into. \c locateAllParticles() runs once
    at start-up to bin particles by cell from the initial state.

    \c lambdaDotModel binds the DEM particle state to the porosity and
    Us update path used by the solver. Its \c update() drives the
    per-cell solid velocity aggregation (UsDEM) and the Laplace-smoothed
    solid velocity field Us — see porousGasificationMedia/DEM/lambdaDotModel.

    \c gaussianInterp = false selects a top-hat (cell-centred) particle
    -> cell projection inside the FoamYade coupling. TODO: confirm
    semantics and whether this should ever be true.

\*---------------------------------------------------------------------------*/

bool gaussianInterp = false;

autoPtr<FoamYade> yadeCoupling(nullptr);
autoPtr<lambdaDotModel> lambdaDotUpdater(nullptr);

if (DEM) 
{
  yadeCoupling.reset(new FoamYade(mesh, U, gradP, vGrad, divT, ddtU_f, g,
                              uSourceDrag, alphac, uSource, uParticle, uCoeff,
                              uInterp, lambdaDot, gaussianInterp));

  yadeCoupling().setScalarProperties(partDensity.value(), fluidDensity.value(),
                                   nu.value());

  Info << "Particles properties set." << endl;

  lambdaDotUpdater.reset(new lambdaDotModel(mesh, lambdaDot, nParticles, UsDEM,
                                        Us, porosityF, yadeCoupling()));

  yadeCoupling().locateAllParticles(); // places the location of spheres from time 0
}
