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
#include "OStringStream.H"
#include "IOmanip.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //

void SolidState::rejectReplacedKeys(const dictionary& coeffs) const
{
    OStringStream replacements;

    if (coeffs.found("criticalPorosity"))
    {
        replacements
            << "    criticalPorosity "
            << coeffs.get<scalar>("criticalPorosity") << ";  ->  solidRegimes"
            << " { connectedAboveSolidFraction "
            << 1.0 - coeffs.get<scalar>("criticalPorosity") << "; }" << nl;
    }

    if (coeffs.found("minPorosity"))
    {
        replacements
            << "    minPorosity " << coeffs.get<scalar>("minPorosity")
            << ";  ->  solidRegimes { maxSolidFraction "
            << 1.0 - coeffs.get<scalar>("minPorosity") << "; }" << nl;
    }

    if (coeffs.found("solidStateTolerance"))
    {
        const scalar tol = coeffs.get<scalar>("solidStateTolerance");

        replacements
            << "    solidStateTolerance " << tol << ";  ->  both" << nl
            << "        solidRegimes { dispersedAboveSolidFraction " << tol
            << "; }   (the trace threshold; changes results)" << nl
            << "        solidStateCheckTolerance " << tol
            << ";   (the slack the checks allow; changes none)" << nl;
    }

    if (coeffs.found("gasTemperatureBelowCriticalPorosity"))
    {
        const Switch gas
        (
            coeffs.get<Switch>("gasTemperatureBelowCriticalPorosity")
        );

        replacements
            << "    gasTemperatureBelowCriticalPorosity " << gas
            << ";  ->  solidRegimes { dispersedSolidTemperature "
            << (gas ? "gas" : "solid") << "; }" << nl;
    }

    if (!replacements.str().empty())
    {
        FatalIOErrorInFunction(coeffs)
            << "These keys are no longer read. Replace each as shown:" << nl
            << nl << replacements.str().c_str() << nl
            << "The thresholds are now solid volume fractions, 1 - porosity;"
            << " see doc/case-setup.md." << exit(FatalIOError);
    }
}


bool SolidState::readDispersedSolidTemperature(const dictionary& regimes)
{
    const word rule
    (
        regimes.lookupOrDefault<word>("dispersedSolidTemperature", "solid")
    );

    if (rule != "solid" && rule != "gas")
    {
        FatalIOErrorInFunction(regimes)
            << "dispersedSolidTemperature must be solid or gas, but is "
            << rule << "." << exit(FatalIOError);
    }

    return rule == "gas";
}


void SolidState::checkThresholds(const dictionary& regimes) const
{
    if (connectedAboveSolidFraction_ < 0.0)
    {
        FatalIOErrorInFunction(regimes)
            << "connectedAboveSolidFraction must not be negative, but is "
            << connectedAboveSolidFraction_ << "." << nl
            << "Below 0 the solid source gate stays open in empty cells, where"
            << " rhoCp is at its SMALL floor and any source term gives an"
            << " unbounded Ts step, and no cell is ever emptied. Default 1e-4."
            << exit(FatalIOError);
    }

    if (connectedAboveSolidFraction_ >= 1.0)
    {
        FatalIOErrorInFunction(regimes)
            << "connectedAboveSolidFraction must be below 1, but is "
            << connectedAboveSolidFraction_ << "." << nl
            << "At or above 1 no cell is ever connected, so the radiation,"
            << " chemistry and heat-transfer terms all leave the solid energy"
            << " equation, and every solid cell may be emptied. Default 1e-4."
            << exit(FatalIOError);
    }

    if (maxSolidFraction_ >= 1.0)
    {
        FatalIOErrorInFunction(regimes)
            << "maxSolidFraction must be below 1, but is " << maxSolidFraction_
            << "." << nl
            << "Every gas equation multiplies porosityF straight into its"
            << " ddt coefficient, so a cell packed to zero gas volume leaves"
            << " that matrix with a zero diagonal and the solve divides by"
            << " zero. Pick the largest solid fraction the case is meant to"
            << " reach, not 1."
            << exit(FatalIOError);
    }

    if (dispersedAboveSolidFraction_ < 0.0)
    {
        FatalIOErrorInFunction(regimes)
            << "dispersedAboveSolidFraction must not be negative, but is "
            << dispersedAboveSolidFraction_ << "." << exit(FatalIOError);
    }

    // The regimes must nest: trace < dispersed < connected < maxSolidFraction.
    if (connectedAboveSolidFraction_ >= maxSolidFraction_)
    {
        FatalIOErrorInFunction(regimes)
            << "connectedAboveSolidFraction (" << connectedAboveSolidFraction_
            << ") must be below maxSolidFraction (" << maxSolidFraction_
            << ")." << nl
            << "A cell packed to maxSolidFraction would otherwise never be"
            << " connected, so a full bed could never react."
            << exit(FatalIOError);
    }

    if (dispersedAboveSolidFraction_ >= connectedAboveSolidFraction_)
    {
        FatalIOErrorInFunction(regimes)
            << "dispersedAboveSolidFraction (" << dispersedAboveSolidFraction_
            << ") must be below connectedAboveSolidFraction ("
            << connectedAboveSolidFraction_ << ")." << nl
            << "A cell could otherwise take the solid sources while its Ts"
            << " was still being taken from the gas."
            << exit(FatalIOError);
    }
}


void SolidState::printRegimes() const
{
    Info<< "Solid regimes by solid fraction alpha = 1 - porosity"
        << " (each threshold ends the regime below it):" << nl
        << "    empty      alpha = 0" << nl
        << "    trace      alpha <= " << setw(10) << dispersedAboveSolidFraction()
        << "  Ts follows the gas" << nl
        << "    dispersed  alpha <= " << setw(10) << connectedAboveSolidFraction()
        << "  no solid sources; may be emptied; Ts from the "
        << (dispersedTakesGasTemperature_ ? "gas" : "solid") << nl
        << "    connected  alpha <= " << setw(10) << maxSolidFraction()
        << "  full model" << nl
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
        coeffs.subOrEmptyDict("solidRegimes")
            .lookupOrDefault<scalar>("dispersedAboveSolidFraction", 1e-8)
    ),
    checkTolerance_
    (
        coeffs.lookupOrDefault<scalar>("solidStateCheckTolerance", 1e-8)
    ),
    connectedAboveSolidFraction_
    (
        coeffs.subOrEmptyDict("solidRegimes")
            .lookupOrDefault<scalar>("connectedAboveSolidFraction", 1e-4)
    ),
    maxSolidFraction_
    (
        coeffs.subOrEmptyDict("solidRegimes")
            .lookupOrDefault<scalar>("maxSolidFraction", 0.9999)
    ),
    traceAbovePorosity_(1.0 - dispersedAboveSolidFraction_),
    connectedBelowPorosity_(1.0 - connectedAboveSolidFraction_),
    minPorosity_(1.0 - maxSolidFraction_),
    dispersedTakesGasTemperature_
    (
        readDispersedSolidTemperature(coeffs.subOrEmptyDict("solidRegimes"))
    ),
    solidPresent_
    (
        IOobject
        (
            "solidPresent",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimless, 0.0)
    ),
    solidRegime_
    (
        IOobject
        (
            "solidRegime",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedScalar("empty", dimless, scalar(label(Regime::empty)))
    ),
    connectedAtStepStart_
    (
        IOobject
        (
            "solidConnectedAtStepStart",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimless, 0.0)
    )
{
    rejectReplacedKeys(coeffs);
    checkThresholds(coeffs.subOrEmptyDict("solidRegimes"));
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


tmp<volScalarField> SolidState::totalMass
(
    const PtrList<volScalarField>& Ym
)
{
    tmp<volScalarField> tTotalYm(new volScalarField(0*Ym[0]));
    forAll(Ym, i)
    {
        tTotalYm.ref() += Ym[i];
    }
    return tTotalYm;
}


void SolidState::deriveYiFromYm
(
    const PtrList<volScalarField>& Ym,
    PtrList<volScalarField>& Ys
)
{
    // rho = 1/sum_i(Ys_i/rho_i) is a density only where sum_i Ys_i = 1;
    // a fixedYm patch beside an empty cell injects at that density.
    forAll(Ym[0], cellI)
    {
        scalar Ysum = 0.0;
        forAll(Ys, i)
        {
            Ysum += Ym[i][cellI];
        }

        if (Ysum > SMALL)
        {
            forAll(Ys, i)
            {
                Ys[i][cellI] = Ym[i][cellI] / Ysum;
            }
        }
        else
        {
            // The cell carries no mass, so nothing it holds depends on the
            // choice - only a fixedYm patch beside it reads this density.
            forAll(Ys, i)
            {
                Ys[i][cellI] = 0.0;
            }
            Ys[0][cellI] = 1.0;
        }
    }
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


tmp<volScalarField> SolidState::withDispersedRule
(
    const volScalarField& Ts,
    const volScalarField& Tgas
) const
{
    if (!dispersedTakesGasTemperature_)
    {
        return tmp<volScalarField>(new volScalarField(Ts));
    }

    // pos0 and neg, not pos and neg: pos(0) = neg(0) = 0 would leave a cell
    // exactly at the connected threshold with Ts = 0.
    const volScalarField porosityHeadroomBelowConnected
    (
        connectedBelowPorosity_ - porosity_
    );

    return
        Ts*pos0(porosityHeadroomBelowConnected)
      + Tgas*neg(porosityHeadroomBelowConnected);
}


tmp<volScalarField> SolidState::withTraceRule
(
    const volScalarField& Ts,
    const volScalarField& totalYm,
    const volScalarField& rho,
    const volScalarField& Tgas
) const
{
    const volScalarField traceOnly(isTrace(totalYm, rho));

    return Ts*(1 - traceOnly) + Tgas*traceOnly;
}


tmp<volScalarField> SolidState::withSolidHeatCapacityFloor
(
    const volScalarField& rhoCp
) const
{
    return max
    (
        rhoCp,
        dimensionedScalar("minRhoCp", dimEnergy/dimTemperature/dimVolume, SMALL)
    );
}


tmp<volScalarField> SolidState::gasFractionWithFloor() const
{
    return max
    (
        porosity_,
        dimensionedScalar("gasPorosityFloor", dimless, 1e-4)
    );
}


void SolidState::classifyInitialState()
{
    solidPresent_ = neg(porosity_ - 1);
}


void SolidState::refreshSolidPresent()
{
    forAll(porosity_, cellI)
    {
        if (porosity_[cellI] < 1.0)
        {
            solidPresent_[cellI] = 1.0;
        }
        else
        {
            solidPresent_[cellI] = 0.0;
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


void SolidState::classify
(
    const volScalarField& totalYm,
    const volScalarField& rho
)
{
    const volScalarField beyondTrace(massBeyondTrace(totalYm, rho));

    forAll(solidRegime_, cellI)
    {
        Regime regime = Regime::connected;

        if (solidPresent_[cellI] == 0)
        {
            regime = Regime::empty;
        }
        else if (pos(beyondTrace[cellI]) == 0)
        {
            regime = Regime::trace;
        }
        else if (porosity_[cellI] > connectedBelowPorosity_)
        {
            regime = Regime::dispersed;
        }

        solidRegime_[cellI] = scalar(label(regime));
    }

    solidRegime_.correctBoundaryConditions();
}


void SolidState::correctSolidPresentBoundaryConditions()
{
    solidPresent_.correctBoundaryConditions();
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// ************************************************************************* //
