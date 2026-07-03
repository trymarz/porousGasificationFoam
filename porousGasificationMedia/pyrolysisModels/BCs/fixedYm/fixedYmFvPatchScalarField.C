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

#include "fixedYmFvPatchScalarField.H"
#include "addToRunTimeSelectionTable.H"
#include "fvPatchFieldMapper.H"
#include "volFields.H"
#include "HGSSolidThermo.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

fixedYmFvPatchScalarField::fixedYmFvPatchScalarField
(
    const fvPatch& p,
    const DimensionedField<scalar, volMesh>& iF
)
:
    fixedValueFvPatchScalarField(p, iF)
{}


fixedYmFvPatchScalarField::fixedYmFvPatchScalarField
(
    const fixedYmFvPatchScalarField& ptf,
    const fvPatch& p,
    const DimensionedField<scalar, volMesh>& iF,
    const fvPatchFieldMapper& mapper
)
:
    fixedValueFvPatchScalarField(ptf, p, iF, mapper)
{}


fixedYmFvPatchScalarField::fixedYmFvPatchScalarField
(
    const fvPatch& p,
    const DimensionedField<scalar, volMesh>& iF,
    const dictionary& dict
)
:
    fixedValueFvPatchScalarField(p, iF, dict)
{}


fixedYmFvPatchScalarField::fixedYmFvPatchScalarField
(
    const fixedYmFvPatchScalarField& tppsf
)
:
    fixedValueFvPatchScalarField(tppsf)
{}


fixedYmFvPatchScalarField::fixedYmFvPatchScalarField
(
    const fixedYmFvPatchScalarField& tppsf,
    const DimensionedField<scalar, volMesh>& iF
)
:
    fixedValueFvPatchScalarField(tppsf, iF)
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

void fixedYmFvPatchScalarField::updateCoeffs()
{
    if (updated())
    {
        return;
    }

    const label patchi = patch().index();

    // The Ym fields are named after their Yi with a trailing "m"
    // ("Ycharm" -> "Ychar"), see the volPyrolysis constructor.
    const word& ymName = internalField().name();
    const word yiName(ymName.substr(0, ymName.size() - 1));

    const volScalarField& Yi = db().lookupObject<volScalarField>(yiName);

    const HGSSolidThermo& thermo = db().lookupObject<HGSSolidThermo>
    (
        "solidThermophysicalProperties"
    );

    const volScalarField& porosityF = db().lookupObject<volScalarField>
    (
        "porosityF"
    );

    fvPatchScalarField& rho =
        const_cast<fvPatchScalarField&>(thermo.rho().boundaryField()[patchi]);
    rho.evaluate();

    operator==
    (
        Yi.boundaryField()[patchi]
      * rho
      * (1.0 - porosityF.boundaryField()[patchi])
    );

    fixedValueFvPatchScalarField::updateCoeffs();
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

makePatchTypeField(fvPatchScalarField, fixedYmFvPatchScalarField);

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// ************************************************************************* //
