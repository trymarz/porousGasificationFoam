#ifndef YADE_COUPLING_PGF
bool gaussianInterp = false;
#endif // !YADE_COUPLING_PGF

autoPtr<DemYadeCoupler> yadeCoupling(nullptr);
autoPtr<lambdaDotModel> lambdaDotUpdater(nullptr);

if (DEM) 
{
#if defined(YADE_COUPLING_PGF)
  // Minimal backend: the mesh and lambdaDot are the whole interface.
  yadeCoupling.reset(new DemYadeCoupler(mesh, lambdaDot));
#else
  yadeCoupling.reset(new DemYadeCoupler(mesh, U, gradP, vGrad, divT, ddtU_f, g,
                              uSourceDrag, alphac, uSource, uParticle, uCoeff,
                              uInterp, lambdaDot, gaussianInterp));

  yadeCoupling().setScalarProperties(partDensity.value(), fluidDensity.value(),
                                   nu.value());

  Info << "Particles properties set." << endl;
#endif

  lambdaDotUpdater.reset(new lambdaDotModel(mesh, lambdaDot, nParticles, UsDEM,
                                        Us, porosityF, yadeCoupling()));

#if !defined(YADE_COUPLING_PGF)
  yadeCoupling().locateAllParticles(); // places the location of spheres from time 0
#endif
}
