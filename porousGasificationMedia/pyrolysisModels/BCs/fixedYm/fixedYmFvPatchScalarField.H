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

Class
    Foam::fixedYmFvPatchScalarField

Description
    A fixed boundary condition for the solid-specie mass concentration
    Ym_i [kg/m3].  The Ym fields are constructed by volPyrolysis, not read
    from case files, so the user prescribes boundary conditions on the mass
    fractions Yi instead; wherever Yi carries a fixedValue condition the
    corresponding Ym patch gets this type, which recomputes

        Ym = Yi * rho_s * (1 - porosity)

    from the current boundary fields on every update, tracking rho and
    porosity as they evolve (mirrors the fixedSolidH pattern for T/solidH).
    The Yi field is found by stripping the trailing "m" from the Ym name.

    A fixedYm patch is impermeable to solid mass: solveSpeciesMass() zeroes
    the solid flux on it, because an outflow face would drain the pinned
    value instead of the upwind cell value and thereby remove mass the cell
    does not hold.  Solid leaves the domain through zeroGradient-Yi patches.

SourceFiles
    fixedYmFvPatchScalarField.C

\*---------------------------------------------------------------------------*/

#ifndef fixedYmFvPatchScalarField_H
#define fixedYmFvPatchScalarField_H

#include "fixedValueFvPatchFields.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

/*---------------------------------------------------------------------------*\
                Class fixedYmFvPatchScalarField Declaration
\*---------------------------------------------------------------------------*/

class fixedYmFvPatchScalarField
:
    public fixedValueFvPatchScalarField
{

public:

    //- Runtime type information
    TypeName("fixedYm");


    // Constructors

        //- Construct from patch and internal field
        fixedYmFvPatchScalarField
        (
            const fvPatch&,
            const DimensionedField<scalar, volMesh>&
        );

        //- Construct from patch, internal field and dictionary
        fixedYmFvPatchScalarField
        (
            const fvPatch&,
            const DimensionedField<scalar, volMesh>&,
            const dictionary&
        );

        //- Construct by mapping given fixedYmFvPatchScalarField
        // onto a new patch
        fixedYmFvPatchScalarField
        (
            const fixedYmFvPatchScalarField&,
            const fvPatch&,
            const DimensionedField<scalar, volMesh>&,
            const fvPatchFieldMapper&
        );

        //- Construct as copy
        fixedYmFvPatchScalarField
        (
            const fixedYmFvPatchScalarField&
        );

        //- Construct and return a clone
        virtual tmp<fvPatchScalarField> clone() const
        {
            return tmp<fvPatchScalarField>
            (
                new fixedYmFvPatchScalarField(*this)
            );
        }

        //- Construct as copy setting internal field reference
        fixedYmFvPatchScalarField
        (
            const fixedYmFvPatchScalarField&,
            const DimensionedField<scalar, volMesh>&
        );

        //- Construct and return a clone setting internal field reference
        virtual tmp<fvPatchScalarField> clone
        (
            const DimensionedField<scalar, volMesh>& iF
        ) const
        {
            return tmp<fvPatchScalarField>
            (
                new fixedYmFvPatchScalarField(*this, iF)
            );
        }


    // Member functions

        // Evaluation functions

            //- Update the coefficients associated with the patch field
            virtual void updateCoeffs();
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
