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

#include "solidState.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //

void SolidState::checkThresholds(const dictionary& coeffs) const
{
    if (connectedBelowPorosity_ > 1.0)
    {
        FatalIOErrorInFunction(coeffs)
            << "criticalPorosity must not exceed 1, but is " << connectedBelowPorosity_
            << "." << nl
            << "Above 1 the solid source gate pos(criticalPorosity - porosity)"
            << " stays open in empty cells, where rhoCp is at its SMALL floor"
            << " and any source term gives an unbounded Ts step, and bed"
            << " collapse stops selecting cells. Default 0.9999."
            << exit(FatalIOError);
    }

    if (connectedBelowPorosity_ <= 0.0)
    {
        FatalIOErrorInFunction(coeffs)
            << "criticalPorosity must be positive, but is " << connectedBelowPorosity_
            << "." << nl
            << "At or below 0 the solid source gate pos(criticalPorosity -"
            << " porosity) is shut everywhere, so the radiation, chemistry and"
            << " heat-transfer terms all leave the solid energy equation, and"
            << " bed collapse selects every cell. Default 0.9999."
            << exit(FatalIOError);
    }

    if (minPorosity_ <= 0.0)
    {
        FatalIOErrorInFunction(coeffs)
            << "minPorosity must be positive, but is " << minPorosity_
            << "." << nl
            << "Every gas equation multiplies porosityF straight into its"
            << " ddt coefficient, so a cell packed to zero gas volume leaves"
            << " that matrix with a zero diagonal and the solve divides by"
            << " zero. Pick the smallest gas fraction the case is meant to"
            << " reach, not zero."
            << exit(FatalIOError);
    }

    // In solid fraction alpha = 1 - porosity the regimes must nest:
    // trace < dispersed < connected < maxSolidFraction.
    if (minPorosity_ >= connectedBelowPorosity_)
    {
        FatalIOErrorInFunction(coeffs)
            << "minPorosity (" << minPorosity_ << ") must be below"
            << " criticalPorosity (" << connectedBelowPorosity_ << ")." << nl
            << "A cell packed to minPorosity would otherwise be too empty to"
            << " admit the solid sources, so a full bed could never react."
            << exit(FatalIOError);
    }

    if (dispersedAboveSolidFraction_ >= 1.0 - connectedBelowPorosity_)
    {
        FatalIOErrorInFunction(coeffs)
            << "solidStateTolerance (" << dispersedAboveSolidFraction_
            << ") must be below 1 - criticalPorosity ("
            << 1.0 - connectedBelowPorosity_
            << ")." << nl
            << "The presence floor, in solid volume fraction, would otherwise"
            << " reach the source gate, so a cell could take the solid sources"
            << " while its Ts was still being taken from the gas."
            << exit(FatalIOError);
    }
}


void SolidState::printRegimes() const
{
    Info<< "Solid regimes by solid fraction alpha = 1 - porosity"
        << " (each threshold ends the regime below it):" << nl
        << "    empty      alpha = 0" << nl
        << "    trace      alpha <= " << dispersedAboveSolidFraction()
        << "    Ts follows the gas" << nl
        << "    dispersed  alpha <= " << connectedAboveSolidFraction()
        << "    no solid sources; may be emptied" << nl
        << "    connected  alpha <= " << maxSolidFraction()
        << "    full model" << nl
        << "Solid state check tolerance " << checkTolerance() << nl
        << endl;
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

SolidState::SolidState
(
    const dictionary& coeffs,
    const volScalarField& porosity
)
:
    mesh_(porosity.mesh()),
    porosity_(porosity),
    dispersedAboveSolidFraction_
    (
        coeffs.lookupOrDefault<scalar>("solidStateTolerance", 1e-8)
    ),
    checkTolerance_(dispersedAboveSolidFraction_),
    connectedBelowPorosity_
    (
        coeffs.lookupOrDefault<scalar>("criticalPorosity", 0.9999)
    ),
    minPorosity_(coeffs.lookupOrDefault<scalar>("minPorosity", 1e-4)),
    solidPresent_
    (
        IOobject
        (
            "solidOccupiesCell",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimless, 0.0)
    ),
    absent_
    (
        IOobject
        (
            "solidAbsentFromCell",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedScalar("one", dimless, 1.0)
    ),
    connectedAtStepStart_
    (
        IOobject
        (
            "solidSourceTermsAdmissible",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimless, 0.0)
    )
{
    checkThresholds(coeffs);
    printRegimes();
}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

SolidState::~SolidState()
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

tmp<volScalarField> SolidState::solidFraction() const
{
    return 1 - porosity_;
}


tmp<volScalarField> SolidState::massBeyondTrace
(
    const volScalarField& totalYm,
    const volScalarField& rho
) const
{
    const volScalarField YmFloor
    (
        max
        (
            dispersedAboveSolidFraction_*rho,
            dimensionedScalar("YmFloorMin", dimDensity, SMALL)
        )
    );

    return totalYm - YmFloor;
}


tmp<volScalarField> SolidState::isTrace
(
    const volScalarField& totalYm,
    const volScalarField& rho
) const
{
    return solidPresent_*(1 - pos(massBeyondTrace(totalYm, rho)));
}


tmp<volScalarField> SolidState::recoverTs
(
    const volScalarField& TsFromEnthalpy,
    const volScalarField& totalYm,
    const volScalarField& rho,
    const volScalarField& Tgas
) const
{
    const volScalarField traceOnly(isTrace(totalYm, rho));

    return TsFromEnthalpy*(1 - traceOnly) + Tgas*traceOnly;
}


void SolidState::classifyInitialState()
{
    solidPresent_ = neg(porosity_ - 1);
    absent_ = pos0(porosity_ - 1);
}


void SolidState::refreshSolidPresent()
{
    forAll(porosity_, cellI)
    {
        if (porosity_[cellI] < 1.0)
        {
            solidPresent_[cellI] = 1.0;
            absent_[cellI] = 0.0;
        }
        else
        {
            solidPresent_[cellI] = 0.0;
            absent_[cellI] = 1.0;
        }
    }
}


void SolidState::freezeConnectedAtStepStart()
{
    // solidPresent_ is porosity < 1; the connected test is the stricter
    // of the two at the default thresholds.
    connectedAtStepStart_ =
        solidPresent_*pos(connectedBelowPorosity_ - porosity_);
}


void SolidState::correctSolidPresentBoundaryConditions()
{
    solidPresent_.correctBoundaryConditions();
}


void SolidState::correctAbsentBoundaryConditions()
{
    absent_.correctBoundaryConditions();
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// ************************************************************************* //
