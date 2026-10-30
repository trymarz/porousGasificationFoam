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

#include "solidSpeciesTransport.H"
#include "fvm.H"
#include "fvc.H"
#include "fvMatrices.H"
#include "OStringStream.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

defineTypeNameAndDebug(SolidSpeciesTransport, 0);


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

SolidSpeciesTransport::SolidSpeciesTransport
(
    const fvMesh& mesh,
    const HGSSolidThermo& solidThermo,
    const porousThermoSolidChemistryModel<HGSSolidThermo>& chemistry,
    PtrList<volScalarField>& Ym,
    PtrList<volScalarField>& Ys,
    const volScalarField& porosity,
    const volVectorField& Us,
    const surfaceScalarField& phiSolid,
    LambdaDotCoupling& lambdaDot,
    const SolidStateChecker& solidStateChecker,
    const Switch& active
)
:
    mesh_(mesh),
    solidThermo_(solidThermo),
    chemistry_(chemistry),
    Ym_(Ym),
    Ys_(Ys),
    porosity_(porosity),
    Us_(Us),
    phiSolid_(phiSolid),
    lambdaDot_(lambdaDot),
    solidStateChecker_(solidStateChecker),
    active_(active),
    cumulativeYmClip_(0.0)
{}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

SolidSpeciesTransport::~SolidSpeciesTransport()
{}


// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //

void SolidSpeciesTransport::checkSpecieMass
(
    const label i,
    const volScalarField& divYmFlux,
    const volScalarField& sRhoSi
) const
{
    const label badCell =
        solidStateChecker_.firstInvalidExtensive(Ym_[i]);

    if (badCell != -1)
    {
        OStringStream context;
        context
            << "    specie       = " << Ys_[i].name() << nl
            << "    Us           = " << Us_[badCell] << nl
            << "    div(phiUs Ym)= " << divYmFlux[badCell] << nl
            << "    RRs          = " << sRhoSi[badCell] << nl
            << "    porosity     = " << porosity_[badCell];

        solidStateChecker_.abort
        (
            "the solid specie mass equation in SolidSpeciesTransport::solve()",
            Ym_[i],
            badCell,
            context.str()
        );
    }
}

void SolidSpeciesTransport::clipNegativeSpecieMass(volScalarField& Ym_i)
{
    cumulativeYmClip_ +=
        gSum(max(-Ym_i.field(), 0.0)*mesh_.V());

    Ym_i.max(0.0);                       // mass concentration >= 0
}

void SolidSpeciesTransport::solveSpecieMass(const label i)
{
    volScalarField& Ym_i = Ym_[i];
    volScalarField sRhoSi = chemistry_.RRs(i);

    // lambdaDot only reads sRhoSi, which the Ym equation uses unchanged.
    lambdaDot_.addChemistryTerm
    (
        sRhoSi,
        solidThermo_.composition().components()[i]
    );

    Ym_i.correctBoundaryConditions();

    // The already-limited flux, settled once per step so every solid
    // field rides the same value at each face.
    volScalarField divYmFlux
    (
        fvc::div(phiSolid_, Ym_i, "div(phiSolid)")
    );

    fvScalarMatrix YmEqn
    (
        fvm::ddt(Ym_i)
     ==
        sRhoSi
      - divYmFlux
    );

    YmEqn.relax();
    YmEqn.solve("Ys");

    checkSpecieMass(i, divYmFlux, sRhoSi);
    clipNegativeSpecieMass(Ym_i);

    Info<< "solid " << Ys_[i].name()
        << " equation solved. Sources min/max   = " << gMin(sRhoSi)
        << ", " << gMax(sRhoSi)
        << "; values min Ym = " << gMin(Ym_[i])
        << " max Ym = " << gMax(Ym_[i]) << endl;
}


// * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * * //

void SolidSpeciesTransport::solve()
{
// eqZx2uHGn045
// eqZx2uHGn046

    if (debug)
    {
        Info<< "SolidSpeciesTransport:+solve()" << endl;
    }

    if (!active_)
    {
        return;
    }

    // Reset lambdaDot and add its temperature term; the chemistry
    // term accumulates per specie in solveSpecieMass().
    lambdaDot_.beginStep();

    for (label i = 0; i < Ys_.size(); ++i)
    {
        solveSpecieMass(i);
    }

    SolidState::deriveYiFromYm(Ym_, Ys_);

    // lambdaDot is complete once every specie has contributed.
    lambdaDot_.finishStep();
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// ************************************************************************* //
