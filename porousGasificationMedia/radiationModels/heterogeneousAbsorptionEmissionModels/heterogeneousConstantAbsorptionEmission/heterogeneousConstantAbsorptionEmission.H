/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | foam-extend: Open Source CFD
   \\    /   O peration     |
    \\  /    A nd           | For copyright notice see file Copyright
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

Class
    Foam::radiationModels::heterogeneousConstantAbsorptionEmission

Description
    Constant absorption / emission coefficients.

    Five scalars read from
    \c heterogeneousConstantAbsorptionEmissionCoeffs in
    \c constant/radiationProperties:
      - \c a        — gas-phase absorption coefficient [1/m];
      - \c as       — solid-phase volumetric absorption [1/m];
      - \c borderAs — solid-surface absorption [1/m] (large; only
                      active on bed edge cells);
      - \c E        — gas emission contribution [W/m^3];
      - \c borderL  — surface-layer thickness [m] used by the
                      radiation model to scale the surface absorption.

    Greyness is reported as true. The values are notoriously hard to
    source from literature — README → "Radiation parameters are the
    hardest to source" recommends tuning on a TGA-like reference case
    and reusing.

SourceFiles
    heterogeneousConstantAbsorptionEmission.H
    heterogeneousConstantAbsorptionEmission.C

\*---------------------------------------------------------------------------*/

#ifndef heterogeneousConstantAbsorptionEmission_H
#define heterogeneousConstantAbsorptionEmission_H

#include "heterogeneousAbsorptionEmissionModel.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace radiationModels
{
namespace heterogeneousAbsorptionEmissionModels
{

/*---------------------------------------------------------------------------*\
           Class heterogeneousConstantAbsorptionEmission Declaration
\*---------------------------------------------------------------------------*/

class heterogeneousConstantAbsorptionEmission
:
    public heterogeneousAbsorptionEmissionModel
{
    // Private data

        //- Absorption model dictionary
        dictionary coeffsDict_;

        //- Absorption coefficient / [1/m]
        dimensionedScalar a_;

        //- Absorption coefficient / [1/m]
        dimensionedScalar as_;

        //- Absorption coefficient / [1/m]
        dimensionedScalar borderAs_;

        //- Emission contribution / [kg/(m s^3)]
        dimensionedScalar E_;

        //- Emission contribution / [1/m]
        dimensionedScalar borderL_;

public:

    //- Runtime type information
    TypeName("heterogeneousConstantAbsorptionEmission");


    // Constructors

        //- Construct from components
        heterogeneousConstantAbsorptionEmission
        (
            const dictionary& dict,
            const fvMesh& mesh
        );


    // Destructor
    virtual ~heterogeneousConstantAbsorptionEmission();


    // Member Operators

        // Access

            // Absorption coefficient

                //- Absorption coefficient for continuous phase
                tmp<volScalarField> aCont(const label bandI = 0) const;

                //- Absorption coefficient for continuous phase
                tmp<volScalarField> asCont(const label bandI = 0) const;

                //- Absorption coefficient for continuous phase
                tmp<volScalarField> borderAsCont(const label bandI = 0) const;

            // Emission contribution

                //- Emission contribution for continuous phase
                tmp<volScalarField> ECont(const label bandI = 0) const;

                // Border thickness layer
                virtual dimensionedScalar borderL(const label bandI = 0) const;

    // Member Functions

        inline bool isGrey() const
        {
            return true;
        }
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousAbsorptionEmissionModels
} // End namespace radiationModels
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
