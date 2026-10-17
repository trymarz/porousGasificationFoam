bool gaussianInterp = false;

autoPtr<FoamYade> yadeCoupling(nullptr);
autoPtr<lambdaDotModel> lambdaDotUpdater(nullptr);

if (DEM) 
{
  // Tells Yade at startup how to apply what PGF sends per particle: a rate it
  // integrates (0), or the apparentVolume factor it multiplies lambda by (1).
  const int yadeLambdaMode =
      IOdictionary
      (
          IOobject
          (
              "lambdaDict", runTime.constant(), mesh,
              IOobject::MUST_READ, IOobject::NO_WRITE, false
          )
      ).lookupOrDefault<word>("lambdaMode", "constant") == "apparentVolume"
    ? 1 : 0;

  yadeCoupling.reset(new FoamYade(mesh, U, gradP, vGrad, divT, ddtU_f, g,
                              uSourceDrag, alphac, uSource, uParticle, uCoeff,
                              uInterp, lambdaDot, gaussianInterp,
                              yadeLambdaMode));

  yadeCoupling().setScalarProperties(partDensity.value(), fluidDensity.value(),
                                   nu.value());

  Info << "Particles properties set." << endl;

  lambdaDotUpdater.reset(new lambdaDotModel(mesh, lambdaDot, nParticles, UsDEM,
                                        Us, lambdaDEM, lambda, porosityF,
                                        yadeCoupling()));

  yadeCoupling().locateAllParticles(); // places the location of spheres from time 0
}
