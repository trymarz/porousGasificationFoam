bool gaussianInterp = false;

FoamYade yadeCoupling(mesh, U, gradP, vGrad, divT, ddtU_f, g, uSourceDrag, alphac, uSource, uParticle, uCoeff, uInterp, lambdaDot, gaussianInterp);

yadeCoupling.setScalarProperties(partDensity.value(), fluidDensity.value(), nu.value());
Info<< "Particles properties set." << endl;

lambdaDotModel lambdaDotUpdater(mesh, lambdaDot, nParticles, Us, UsInterp, porosityF, yadeCoupling);

yadeCoupling.locateAllParticles();   // places the location of spheres from time 0
