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

#include "constantLambdaDot.H"

namespace Foam
{

constantLambdaDot::constantLambdaDot
(
    const dictionary& dict,
    const fvMesh& mesh,
    volScalarField& lambdaDot
)
:
    LambdaDotCalculationModel(dict, mesh, lambdaDot),
    // Read as a bare scalar (dicts carry no dimensions) and wrapped in [m/s];
    // assigned bare it would be dimensionless and fail lambdaDot_'s check.
    lambdaValue_
    (
        dimensionedScalar
        (
            "lambdaValue",
            dimLength/dimTime,
            dict.lookupOrDefault<scalar>("lambdaValue", 0.0)
        )
    )
{}


void constantLambdaDot::calculateTemperatureDriven()
{
    // Uniform lambdaDot from constant/lambdaDict.
    lambdaDot_ += lambdaValue_;
}

} // namespace Foam
