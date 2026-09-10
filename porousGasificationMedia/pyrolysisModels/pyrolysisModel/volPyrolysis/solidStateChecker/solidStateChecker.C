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

#include "solidStateChecker.H"

#include "PstreamReduceOps.H"

#include <cmath>

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //

label SolidStateChecker::firstInvalidCell
(
    const volScalarField& fld,
    const scalar lower,
    const scalar upper,
    const scalarField* mask
) const
{
    forAll(fld, cellI)
    {
        if (mask && (*mask)[cellI] <= 0.0)
        {
            continue;
        }

        const scalar value = fld[cellI];

        if (!std::isfinite(value) || value < lower || value > upper)
        {
            return cellI;
        }
    }

    return -1;
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

SolidStateChecker::SolidStateChecker
(
    const dictionary& coeffs,
    const fvMesh& mesh,
    const Time& time
)
:
    mesh_(mesh),
    time_(time),
    failOnInvalidSolidState_
    (
        coeffs.lookupOrDefault("failOnInvalidSolidState",true)
    ),
    solidStateTolerance_
    (
        coeffs.lookupOrDefault<scalar>("solidStateTolerance",1e-8)
    ),
    maxSolidTemperature_
    (
        coeffs.lookupOrDefault<scalar>("maxSolidTemperature",1e5)
    )
{
    Info << "failOnInvalidSolidState  " << failOnInvalidSolidState_ << endl;
    Info << "solidStateTolerance      " << solidStateTolerance_ << endl;
    Info << "maxSolidTemperature      " << maxSolidTemperature_ << endl;
}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

SolidStateChecker::~SolidStateChecker()
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

label SolidStateChecker::firstInvalidPorosity
(
    const volScalarField& por
) const
{
    if (!failOnInvalidSolidState_)
    {
        return -1;
    }

    return firstInvalidCell
    (
        por,
        -solidStateTolerance_,
        1.0 + solidStateTolerance_
    );
}


label SolidStateChecker::firstInvalidExtensive
(
    const volScalarField& fld
) const
{
    if (!failOnInvalidSolidState_)
    {
        return -1;
    }

    const scalar fldScale = max(gMax(fld), SMALL);

    return firstInvalidCell(fld, -solidStateTolerance_*fldScale, GREAT);
}


label SolidStateChecker::firstInvalidTemperature
(
    const volScalarField& T,
    const scalarField& mask
) const
{
    if (!failOnInvalidSolidState_)
    {
        return -1;
    }

    return firstInvalidCell
    (
        T,
       -solidStateTolerance_*maxSolidTemperature_,
        maxSolidTemperature_,
        &mask
    );
}


label SolidStateChecker::firstInvalidTemperature
(
    const volScalarField& T
) const
{
    if (!failOnInvalidSolidState_)
    {
        return -1;
    }

    return firstInvalidCell(T, 0.0, maxSolidTemperature_);
}


void SolidStateChecker::abort
(
    const word& stage,
    const volScalarField& fld,
    const label cellI,
    const string& context
) const
{
    FatalErrorInFunction
        << "Impossible solid state produced by " << stage << nl << nl
        << "    time         = " << time_.timeName() << nl
        << "    deltaT       = " << time_.deltaTValue() << nl
        << "    field        = " << fld.name() << nl
        << "    value        = " << fld[cellI] << nl
        << "    processor    = " << Pstream::myProcNo() << nl
        << "    cell         = " << cellI << nl
        << "    cell centre  = " << mesh_.C()[cellI] << nl
        << "    cell volume  = " << mesh_.V()[cellI] << nl
        << context.c_str() << nl
        << "The solid transport equations are explicit and unbounded. Either"
        << " the solid velocity Us is invalid, or the explicit update has"
        << " overshot at this time step." << nl
        << exit(FatalError);
}


tmp<volScalarField> SolidStateChecker::solidPresent
(
    const volScalarField& totalYm,
    const volScalarField& rho
) const
{
    // The smallest solid mass distinguishable from zero: rho is the skeletal
    // density, so a cell below solidStateTolerance_ of a packed cell holds
    // nothing. Floored by SMALL too, since rho is itself zero where empty.
    const volScalarField YmFloor
    (
        max
        (
            solidStateTolerance_*rho,
            dimensionedScalar("YmFloorMin", dimDensity, SMALL)
        )
    );

    return totalYm - YmFloor;
}


void SolidStateChecker::checkConsistency
(
    const volScalarField& porosity,
    const PtrList<volScalarField>& Ym,
    const volScalarField& rho,
    const volScalarField& whereIs
) const
{
    scalar maxResidual = 0.0;
    label worstCell = -1;

    forAll(porosity, cellI)
    {
        scalar cellYm = 0.0;
        forAll(Ym, i)
        {
            cellYm += Ym[i][cellI];
        }

        const scalar residual = mag
        (
            1.0 - porosity[cellI] - cellYm/max(rho[cellI], SMALL)
        );

        if (residual > maxResidual)
        {
            maxResidual = residual;
            worstCell = cellI;
        }
    }

    const scalar globalResidual =
        returnReduce(maxResidual, maxOp<scalar>());

    Info<< "solid state consistency: max|1 - porosity"
        << " - sum(Ym_i/rho_i)| = " << globalResidual << endl;

    // The last two conditions leave the report to the rank that owns the
    // worst cell.
    if
    (
        globalResidual > solidStateTolerance_
     && worstCell != -1
     && maxResidual == globalResidual
    )
    {
        WarningInFunction
            << "porosity and solid mass disagree by " << maxResidual
            << " in cell " << worstCell << " at "
            << mesh_.C()[worstCell] << ": porosity = "
            << porosity[worstCell] << ", whereIs = "
            << whereIs[worstCell]
            << ". A porosity written after the recovery cannot be"
            << " reconciled with the mass the cell holds." << endl;
    }
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// ************************************************************************* //
