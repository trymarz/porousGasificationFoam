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
                                        Us, lambdaDEM, lambda, porosityF,
                                        yadeCoupling()));

  yadeCoupling().locateAllParticles(); // places the location of spheres from time 0
}
