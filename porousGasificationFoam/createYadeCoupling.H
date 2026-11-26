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

autoPtr<pgfDemCoupler> pgfCoupling(nullptr);
autoPtr<lambdaDotModel> lambdaDotUpdater(nullptr);

if (DEM)
{
  // Tells Yade at startup how to apply what PGF sends per particle: a rate it
  // integrates, or the apparentVolume factor it multiplies lambda by.
  const PgfDemBridge::LambdaMode yadeLambdaMode =
      IOdictionary
      (
          IOobject
          (
              "lambdaDict", runTime.constant(), mesh,
              IOobject::MUST_READ, IOobject::NO_WRITE, false
          )
      ).lookupOrDefault<word>("lambdaMode", "constant") == "apparentVolume"
    ? PgfDemBridge::LambdaMode::apparentVolume
    : PgfDemBridge::LambdaMode::rate;

  pgfCoupling.reset(new pgfDemCoupler(mesh, lambdaDot, yadeLambdaMode));

  lambdaDotUpdater.reset(new lambdaDotModel(mesh, lambdaDot, nParticles, UsDEM,
                                        Us, lambdaDEM, lambda, porosityF,
                                        pgfCoupling->particles()));
}
