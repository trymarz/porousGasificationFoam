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

#include "solidDiagnostics.H"

#include "PstreamReduceOps.H"
#include "fvcVolumeIntegrate.H"

#include <cmath>

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //

label SolidDiagnostics::firstInvalidCell
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


scalar SolidDiagnostics::totalYmMass() const
{
    scalar total = 0.0;
    for (label i = 0; i < Ym_.size(); ++i)
    {
        total += gSum(Ym_[i].field() * mesh_.V());
    }
    return total;
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

SolidDiagnostics::SolidDiagnostics
(
    const dictionary& coeffs,
    const SolidFields& solid
)
:
    mesh_(solid.mesh),
    time_(solid.mesh.time()),
    failOnInvalidSolidState_
    (
        coeffs.lookupOrDefault("failOnInvalidSolidState",true)
    ),
    solidState_(solid.state),
    Ym_(solid.Ym),
    Ys_(solid.Ys),
    chemistry_(solid.chemistry),
    initialYmMass_(totalYmMass()),
    lostSolidMass_(dimensionedScalar("zero", dimMass, 0.0)),
    addedGasMass_(dimensionedScalar("zero", dimMass, 0.0)),
    totalGasMassFlux_(dimensionedScalar("zero", dimMass/dimTime, 0.0)),
    totalHeatRR_(dimensionedScalar("zero", dimEnergy/dimTime, 0.0)),
    maxSolidTemperature_
    (
        coeffs.lookupOrDefault<scalar>("maxSolidTemperature",1e5)
    )
{
    Info << "failOnInvalidSolidState  " << failOnInvalidSolidState_ << endl;
    Info << "maxSolidTemperature      " << maxSolidTemperature_ << endl;
}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

SolidDiagnostics::~SolidDiagnostics()
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

label SolidDiagnostics::firstInvalidPorosity
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
        solidState_.minPorosity() - solidState_.checkTolerance(),
        1.0 + solidState_.checkTolerance()
    );
}


label SolidDiagnostics::firstInvalidExtensive
(
    const volScalarField& fld
) const
{
    if (!failOnInvalidSolidState_)
    {
        return -1;
    }

    const scalar fldScale = max(gMax(fld), SMALL);

    return firstInvalidCell(fld, -solidState_.checkTolerance()*fldScale, GREAT);
}


label SolidDiagnostics::firstInvalidTemperature
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
       -solidState_.checkTolerance()*maxSolidTemperature_,
        maxSolidTemperature_,
        &mask
    );
}


label SolidDiagnostics::firstInvalidTemperature
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


void SolidDiagnostics::abort
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


void SolidDiagnostics::checkConsistency
(
    const volScalarField& porosity,
    const PtrList<volScalarField>& Ym,
    const volScalarField& rho
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

    // Last two conditions: only the rank owning the worst cell reports it.
    if
    (
        globalResidual > solidState_.checkTolerance()
     && worstCell != -1
     && maxResidual == globalResidual
    )
    {
        WarningInFunction
            << "porosity and solid mass disagree by " << maxResidual
            << " in cell " << worstCell << " at "
            << mesh_.C()[worstCell] << ": porosity = "
            << porosity[worstCell] << ", solidPresent = "
            << solidState_.solidPresent()[worstCell]
            << ". A porosity written after the recovery cannot be"
            << " reconciled with the mass the cell holds." << endl;
    }
}


void SolidDiagnostics::reportMassBudget
(
    const scalar clipped,
    const scalar flipped,
    const scalar collapsed
) const
{
    Info<< "solid mass budget: sum(Ym_i * V) = " << totalYmMass()
        << ", initial = " << initialYmMass_
        << ", fabricated by clip = " << clipped
        << ", destroyed by flip = " << flipped
        << ", destroyed by collapse = " << collapsed
        << endl;
}


void SolidDiagnostics::reportDerivedSpecies() const
{
    for (label i = 0; i < Ys_.size(); ++i)
    {
        Info<< "       derived " << Ys_[i].name()
            << " min/max = " << gMin(Ys_[i]) << " / " << gMax(Ys_[i]) << endl;
    }
}


void SolidDiagnostics::accumulateRunTotals(const volScalarField& chemistrySh)
{
    totalGasMassFlux_ = fvc::domainIntegrate(chemistry_.RRg());
    totalHeatRR_ = fvc::domainIntegrate(chemistrySh);

    addedGasMass_ += fvc::domainIntegrate(chemistry_.RRg()) * time_.deltaT();
    lostSolidMass_ += fvc::domainIntegrate(chemistry_.RRs()) * time_.deltaT();
}


void SolidDiagnostics::reportRunTotals(const scalar replenished) const
{
    Info<< indent << "Total gas mass produced  [kg] = " << addedGasMass_.value() << nl
        << indent << "Total solid mass lost    [kg] = " << lostSolidMass_.value() << nl
        << indent << "Total mass replenished   [kg] = " << replenished << nl
        << indent << "Realese rate of pyrolysis gases  [kg/s] = " << totalGasMassFlux_.value() << nl
        << indent << "Heat release rate [J/s] = " << totalHeatRR_.value() << nl;
}


void SolidDiagnostics::reportTemperatureRange(const volScalarField& T) const
{
    scalar minTemp = GREAT;
    scalar maxTemp = -GREAT;
    scalar areThere = 0;
    forAll(T,cellI)
    {
        if (solidState_.solidPresent()[cellI] == 1 )
        {
            areThere = 1;
            if (T[cellI] < minTemp)
            {
                minTemp = T[cellI];
            }
            if (T[cellI] > maxTemp)
            {
                maxTemp = T[cellI];
            }
        }
    }
    reduce(maxTemp, maxOp<scalar>());
    reduce(minTemp, minOp<scalar>());
    reduce(areThere, maxOp<scalar>());
    if (areThere == 1)
    {
        Info<< " pyrolysis min/max(T) = " << minTemp << ", " << maxTemp << endl;
    }
    else
    {
        Info<< " no solid phase " << endl;
    }
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// ************************************************************************* //
