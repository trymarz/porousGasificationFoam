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

  // Ts is registered by the solid thermo model (created in
  // createPyrolysisModel.H, included before this file). It is kept for
  // interface compatibility but no longer drives shrinkage.
  const volScalarField& Ts = mesh.lookupObject<volScalarField>("Ts");

  // Ychar is the solid char mass fraction created by basicSolidMixture from
  // solidThermophysicalProperties( solidComponents ( wood char ) ). It is
  // guaranteed to exist on the mesh by the time this file runs (included
  // after createPyrolysisModel.H) and drives the per-cell shrink factor
  // inside lambdaDotModel::update().
  const volScalarField& Ychar = mesh.lookupObject<volScalarField>("Ychar");

  lambdaDotUpdater.reset(new lambdaDotModel(mesh, lambdaDot, nParticles, UsDEM,
                                        Us, porosityF, Ts, Ychar, yadeCoupling()));

  yadeCoupling().locateAllParticles(); // places the location of spheres from time 0
}
