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

#ifndef lambdaDotModel_H
#define lambdaDotModel_H

#include "DynamicList.H"
#include "Map.H"
#include "autoPtr.H"
#include "fvMesh.H"
#include "foamYadeApiCheck.H"

namespace Foam
{

template<class Type> class InterpolationModel;

class lambdaDotModel
{
private:
    const fvMesh& mesh_;
    volScalarField& lambdaDot_;
    volScalarField& nParticles_;  // number of particles
    volVectorField& UsDEM_;     // velocity of spheres
    volVectorField& Us_;    // interpolated sphere velocity
    volScalarField& lambdaDEM_;   // raw per-cell average of the particles' lambda
    volScalarField& lambda_;      // interpolated particle length scale
    volScalarField& porosityF_;   // from the main solver
    scalar connectedBelowPorosity_; // from constant/pyrolysisProperties
    scalar lambdaDotNeutral_;     // what lambdaDot_ gates to: 0 rate, 1 factor

    FoamYade& yade_;

    // Us from UsDEM: raw per-cell average, smoothed over the solid region.
    Switch interpolateUs_;
    scalar solidPorosityCutoff_;
    autoPtr<InterpolationModel<vector>> usInterpolationModel_;

    // lambda from lambdaDEM, the same way, on its own dictionary keys.
    Switch interpolateLambda_;
    scalar lambdaBackgroundValue_;
    autoPtr<InterpolationModel<scalar>> lambdaInterpolationModel_;

    // Cell-to-cell particle moves since the last exchange.
    Map<label> previousParticleCell_;
    DynamicList<label> movedFromCells_;
    DynamicList<label> movedToCells_;

public:
    lambdaDotModel
    (
        const fvMesh& mesh,
        volScalarField& lambdaDot,
        volScalarField& nParticles,
        volVectorField& UsDEM,   // velocity of spheres
        volVectorField& Us, // interpolated sphere velocity
        volScalarField& lambdaDEM, // raw per-cell average of particle lambda
        volScalarField& lambda, // interpolated particle length scale
        volScalarField& porosityF, // from the main solver
        FoamYade& yade
    );

    ~lambdaDotModel();

    //- Zero lambdaDot outside the mechanically active solid region before
    //  particles read it (volPyrolysis assembles the value itself).
    void updateLambdaDot();

    //- Pull per-particle state from YADE onto the mesh (count, UsDEM_,
    //  lambdaDEM_), smooth each into its continuous field (Us_, lambda_),
    //  then push lambdaDot_ back out to the particles.
    void updateParticleFields();

    const DynamicList<label>& movedFromCells() const;
    const DynamicList<label>& movedToCells() const;

};

} // namespace Foam

#endif
