/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright held by original author
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

\*---------------------------------------------------------------------------*/

// false selects FoamYade's point-force path: Stokes drag and torque per
// particle, sent to YADE with lambdaDot every exchange (doc/theory.md,
// "DEM coupling"). PGF never uses the Gaussian-interpolated path.
bool gaussianInterp = false;

autoPtr<FoamYade> yadeCoupling(nullptr);
autoPtr<pgfYadeCoupler> pgfCoupling(nullptr);
autoPtr<lambdaDotModel> lambdaDotUpdater(nullptr);

// Particles located on this rank at the last exchange, for lambdaDotModel;
// pgfYadeCoupler fills its own list, the legacy path copies FoamYade's.
demParticleList foamYadeParticles;
auto collectFoamYadeParticles = [&]()
{
    foamYadeParticles.clear();
    for (const auto& procPtr : yadeCoupling->inCommProcs)
    {
        if (!procPtr) continue;
        for (const auto& partPtr : procPtr->foundParticles)
        {
            if (!partPtr) continue;
            foamYadeParticles.append
            (
                demParticle
                {
                    partPtr->indx, partPtr->inCell,
                    partPtr->linearVelocity, partPtr->lambda
                }
            );
        }
    }
};

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

  if (demCoupler == "pgf")
  {
    pgfCoupling.reset
    (
        new pgfYadeCoupler
        (
            mesh, lambdaDot, yadeLambdaMode,
            yadeCouplingProperties.lookupOrDefault<scalar>
            (
                "handshakeTimeout", 60
            )
        )
    );
  }
  else
  {
    yadeCoupling.reset(new FoamYade(mesh, U, gradP, vGrad, divT, ddtU_f, g,
                                uSourceDrag, alphac, uSource, uParticle, uCoeff,
                                uInterp, lambdaDot, gaussianInterp,
                                yadeLambdaMode));

    yadeCoupling().setScalarProperties(partDensity.value(),
                                     fluidDensity.value(), nu.value());

    Info << "Particles properties set." << endl;
  }

  lambdaDotUpdater.reset(new lambdaDotModel(mesh, lambdaDot, nParticles, UsDEM,
                                        Us, lambdaDEM, lambda, porosityF,
                                        pgfCoupling.valid()
                                      ? pgfCoupling->particles()
                                      : foamYadeParticles));

  if (yadeCoupling.valid())
  {
    yadeCoupling().locateAllParticles(); // places the location of spheres from time 0
  }
}
