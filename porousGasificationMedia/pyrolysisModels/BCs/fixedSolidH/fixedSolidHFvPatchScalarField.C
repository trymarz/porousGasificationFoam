/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | foam-extend: Open Source CFD
   \\    /   O peration     | Version:     4.1
    \\  /    A nd           | Web:         http://www.foam-extend.org
     \\/     M anipulation  | For copyright notice see file Copyright
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

\*---------------------------------------------------------------------------*/

#include "fixedSolidHFvPatchScalarField.H"
#include "addToRunTimeSelectionTable.H"
#include "fvPatchFieldMapper.H"
#include "volFields.H"
#include "HGSSolidThermo.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

fixedSolidHFvPatchScalarField::fixedSolidHFvPatchScalarField
(
    const fvPatch& p,
    const DimensionedField<scalar, volMesh>& iF
)
:
    fixedValueFvPatchScalarField(p, iF)
{}


fixedSolidHFvPatchScalarField::fixedSolidHFvPatchScalarField
(
    const fixedSolidHFvPatchScalarField& ptf,
    const fvPatch& p,
    const DimensionedField<scalar, volMesh>& iF,
    const fvPatchFieldMapper& mapper
)
:
    fixedValueFvPatchScalarField(ptf, p, iF, mapper)
{}


fixedSolidHFvPatchScalarField::fixedSolidHFvPatchScalarField
(
    const fvPatch& p,
    const DimensionedField<scalar, volMesh>& iF,
    const dictionary& dict
)
:
    fixedValueFvPatchScalarField(p, iF, dict)
{}


fixedSolidHFvPatchScalarField::fixedSolidHFvPatchScalarField
(
    const fixedSolidHFvPatchScalarField& tppsf
)
:
    fixedValueFvPatchScalarField(tppsf)
{}


fixedSolidHFvPatchScalarField::fixedSolidHFvPatchScalarField
(
    const fixedSolidHFvPatchScalarField& tppsf,
    const DimensionedField<scalar, volMesh>& iF
)
:
    fixedValueFvPatchScalarField(tppsf, iF)
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

void fixedSolidHFvPatchScalarField::updateCoeffs()
{
    if (updated())
    {
        return;
    }

    const label patchi = patch().index();

    const HGSSolidThermo& thermo = db().lookupObject<HGSSolidThermo>
    (
        "solidThermophysicalProperties"
    );

    const volScalarField& Ts = db().lookupObject<volScalarField>
    (
        "Ts"
    );

    const volScalarField& porosityF = db().lookupObject<volScalarField>
    (
        "porosityF"
    );

    const tmp<volScalarField> Cp = thermo.Cp();

    fvPatchScalarField& rho =
        const_cast<fvPatchScalarField&>(thermo.rho().boundaryField()[patchi]);
    rho.evaluate();

    operator==(rho * (porosityF.boundaryField()[patchi] - 1.) * Cp().boundaryField()[patchi] * Ts.boundaryField()[patchi]);


    fixedValueFvPatchScalarField::updateCoeffs();
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

makePatchTypeField(fvPatchScalarField, fixedSolidHFvPatchScalarField);

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// ************************************************************************* //
