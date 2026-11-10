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

#include "LambdaDotCalculationModel.H"
#include "constantLambdaDot.H"
#include "exactDifferentialLambdaDot.H"
#include "apparentVolumeLambdaDot.H"

namespace Foam
{

LambdaDotCalculationModel::LambdaDotCalculationModel
(
    const dictionary&,
    const fvMesh& mesh,
    volScalarField& lambdaDot
)
:
    mesh_(mesh),
    lambdaDot_(lambdaDot)
{}


void LambdaDotCalculationModel::beginStep()
{
    lambdaDot_ = dimensionedScalar("zero", lambdaDot_.dimensions(), 0.0);
}


autoPtr<LambdaDotCalculationModel> LambdaDotCalculationModel::New
(
    const dictionary& dict,
    const fvMesh& mesh,
    volScalarField& lambdaDot
)
{
    const word modelName
    (
        dict.lookupOrDefault<word>("lambdaMode", "constant")
    );

    Info<< "Selecting lambdaDot calculation model: "
        << modelName << nl << endl;

    if (modelName == "constant")
    {
        return autoPtr<LambdaDotCalculationModel>
        (
            new constantLambdaDot(dict, mesh, lambdaDot)
        );
    }
    else if (modelName == "exactDifferential")
    {
        return autoPtr<LambdaDotCalculationModel>
        (
            new exactDifferentialLambdaDot(dict, mesh, lambdaDot)
        );
    }

    else if (modelName == "apparentVolume")
    {
        return autoPtr<LambdaDotCalculationModel>
        (
            new apparentVolumeLambdaDot(dict, mesh, lambdaDot)
        );
    }

    // No "off" mode: exactDifferential with every coefficient at its 0.0
    // default gives lambdaDot = 0 exactly.
    FatalErrorInFunction
        << "Unknown lambdaMode '" << modelName << "'." << nl
        << "Valid options are: constant, exactDifferential, apparentVolume"
        << exit(FatalError);

    return autoPtr<LambdaDotCalculationModel>();
}

} // namespace Foam
