/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | foam-extend: Open Source CFD
   \\    /   O peration     |
    \\  /    A nd           | For copyright notice see file Copyright
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of foam-extend.

    foam-extend is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the
    Free Software Foundation, either version 3 of the License, or (at your
    option) any later version.

    foam-extend is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with foam-extend.  If not, see <http://www.gnu.org/licenses/>.

Description
    Implementation of \c heterogeneousNoAbsorptionEmission.
    See the class Description in the .H.

    Constructor and destructor only — all absorption/emission
    coefficients are zero by delegation to the
    \c heterogeneousAbsorptionEmissionModel base-class defaults.
    Selected when radiation transport is active but no
    absorption/emission from the bed is desired.

\*---------------------------------------------------------------------------*/

#include "heterogeneousNoAbsorptionEmission.H"
#include "addToRunTimeSelectionTable.H"

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

namespace Foam
{
namespace radiationModels
{
namespace heterogeneousAbsorptionEmissionModels
{
    defineTypeNameAndDebug(heterogeneousNoAbsorptionEmission, 0);

    addToRunTimeSelectionTable
    (
        heterogeneousAbsorptionEmissionModel,
        heterogeneousNoAbsorptionEmission,
        dictionary
    );
}
}
}

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::radiationModels::heterogeneousAbsorptionEmissionModels::heterogeneousNoAbsorptionEmission::heterogeneousNoAbsorptionEmission
(
    const dictionary& dict,
    const fvMesh& mesh
)
:
    heterogeneousAbsorptionEmissionModel(dict, mesh)
{}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

Foam::radiationModels::heterogeneousAbsorptionEmissionModels::heterogeneousNoAbsorptionEmission::~heterogeneousNoAbsorptionEmission()
{}


// ************************************************************************* //
