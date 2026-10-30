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

#include "porosityRecovery.H"
#include "fvc.H"
#include "FIFOStack.H"
#include "OStringStream.H"
#include "processorPolyPatch.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

PorosityRecovery::PorosityRecovery
(
    const dictionary& coeffs,
    const fvMesh& mesh,
    volScalarField& porosity,
    volScalarField& T,
    const volScalarField& rho,
    PtrList<volScalarField>& Ym,
    PtrList<volScalarField>& Ys,
    autoPtr<volScalarField>& solidH,
    const volVectorField& Us,
    const surfaceScalarField& phiSolid,
    const porousThermoSolidChemistryModel<HGSSolidThermo>& chemistry,
    volScalarField& cellOnBedBorder,
    SolidState& solidState,
    BedCollapse& bedCollapse,
    const autoPtr<SolidStateChecker>& solidStateChecker,
    const Switch& active,
    const Switch& infoOutput
)
:
    mesh_(mesh),
    porosity_(porosity),
    T_(T),
    rho_(rho),
    Ym_(Ym),
    Ys_(Ys),
    solidH_(solidH),
    Us_(Us),
    phiSolid_(phiSolid),
    chemistry_(chemistry),
    cellOnBedBorder_(cellOnBedBorder),
    solidState_(solidState),
    bedCollapse_(bedCollapse),
    solidStateChecker_(solidStateChecker),
    active_(active),
    infoOutput_(infoOutput),
    emptyFlippedCells_
    (
        coeffs.lookupOrDefault("emptyFlippedCells",false)
    ),
    poroProtectSolidInflowFluxTolerance_
    (
        coeffs.lookupOrDefault
        (
            "poroProtectSolidInflowFluxTolerance",
            1e-12
        )
    ),
    porositySource_
    (
        IOobject
        (
            "porosityS",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimless/dimTime, 0.0)
    ),
    cumulativeFlipMass_(0.0)
{}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

PorosityRecovery::~PorosityRecovery()
{}


// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //

void PorosityRecovery::guardPorosity
(
    const word& stage,
    const volScalarField& por,
    const surfaceScalarField& phiUs
) const
{
    const label badCell = solidStateChecker_->firstInvalidPorosity(por);

    if (badCell == -1)
    {
        return;
    }

    // Read from Ym_ here rather than taken as an argument, so a caller
    // after the bed motion reports the mass it actually left behind.
    scalar sumYm = 0.0;
    forAll(Ym_, i)
    {
        sumYm += Ym_[i][badCell];
    }

    OStringStream context;
    context
        << "    maxSolidFraction = " << solidState_.maxSolidFraction() << nl
        << "    Us           = " << Us_[badCell] << nl
        << "    div(phiUs)   = " << fvc::div(phiUs)()[badCell] << nl
        << "    sum(Ym)      = " << sumYm << nl
        << "    rho          = " << rho_[badCell] << nl
        << "    solidPresent = " << solidState_.solidPresent()[badCell];

    solidStateChecker_->abort(stage, por, badCell, context.str());
}

void PorosityRecovery::porosityFromSolidMass(const volScalarField& totalYm)
{
    volScalarField& por = porosity_;

    // por is not transported: 1 - por = sum_i Ym_i/rho_i = totalYm/rho_,
    // an identity from multiComponentSolidMixture's 1/rho_ = sum Ys_i/rho_i.
    // Internal field only: a fixedValue porosity patch keeps its value.
    const dimensionedScalar rhoSolidFloor
    (
        "rhoSolidFloor",
        dimDensity,
        SMALL
    );

    const volScalarField voidFraction
    (
        1.0 - totalYm/max(rho_, rhoSolidFloor)
    );

    por.primitiveFieldRef() = voidFraction.primitiveField();

    // On the mass the transport just produced, before the bed motion or
    // the emptying of flipped cells writes porosity_ again.
    guardPorosity
    (
        "the porosity recovery in PorosityRecovery::recover()",
        por,
        phiSolid_
    );

    Info<< "porosity recovered from solid mass. Chemistry source (not"
        << " applied) min/max   = " << gMin(porositySource_)
        << ", " << gMax(porositySource_);

    Info<< "; values min Y = " << gMin(por)
        <<" max Y = " << gMax(por) << endl;
}

labelList PorosityRecovery::cellsToEmpty
(
    const volScalarField& totalYm,
    label& nFlips
)
{
    FIFOStack<label> candidateStack = {};

    volVectorField solidPresenceGrad = fvc::grad(solidState_.solidPresent());

    // A dispersed or trace cell may be emptied where solid surrounds it
    // or is everywhere absent around it (interior), or where the solid
    // is moving away from the gas (receding edge).
    forAll(porosity_,cellI)
    {
        const vector& presenceGrad = solidPresenceGrad[cellI];
        const bool interior = mag(presenceGrad) == 0;
        const bool recedingEdge =
            mag(presenceGrad) > 0 && (Us_[cellI] & presenceGrad) > 0;

        if
        (
            solidState_.isDispersedOrTrace(cellI)
         && (interior || recedingEdge)
        )
        {
            candidateStack.push(cellI);
        }
    }

    // After the candidates: they read the gradient of last step's mask.
    solidState_.refreshSolidPresent();

    // Do not empty a cell while solid is still filling it through the
    // advective transport equation (an advancing edge).
    volScalarField divPhiYm
    (
        fvc::div(phiSolid_, totalYm, "div(phiSolid)")
    );

    label nCandidates = candidateStack.size();
    label nProtected = 0;
    FIFOStack<label> flipStack = {};

    while (!candidateStack.empty())
    {
        const label cellI = candidateStack.pop();

        if
        (
            divPhiYm[cellI]
          < -poroProtectSolidInflowFluxTolerance_
        )
        {
            ++nProtected;
        }
        else
        {
            flipStack.push(cellI);
        }
    }

    nFlips = flipStack.size();

    reduce(nCandidates, sumOp<label>());
    reduce(nProtected, sumOp<label>());
    reduce(nFlips, sumOp<label>());

    if (infoOutput_ && Pstream::master() && nCandidates > 0)
    {
        Info<< "solid flip guard: candidates=" << nCandidates
            << " vetoed(incoming-solid)=" << nProtected
            << " flipped=" << nFlips
            << endl;
    }

    return labelList(flipStack);
}

List<Field<label>> PorosityRecovery::gatherFlipLists
(
    const labelList& flipCells
) const
{
    List<Field<label>> procFlipList(Pstream::nProcs());
    procFlipList[Pstream::myProcNo()] = flipCells;
    Pstream::gatherList(procFlipList);
    Pstream::scatterList(procFlipList);

    return procFlipList;
}

void PorosityRecovery::emptyCells(const labelList& flipCells, const label nFlips)
{
    // Emptying a cell removes its solid outright rather than just
    // rewriting porosity; the mass is destroyed (no destination) and
    // charged to cumulativeFlipMass_. T_ zeroed to match solidH_.
    const scalarField& V = mesh_.V();
    scalar flippedMass = 0.0;

    forAll(flipCells,entI)
    {
        const label cellI = flipCells[entI];

        forAll(Ym_, i)
        {
            flippedMass += Ym_[i][cellI]*V[cellI];
            Ym_[i][cellI] = 0.0;
        }

        solidH_()[cellI] = 0.0;
        T_[cellI] = 0.0;
        porosity_[cellI] = 1.0;
    }

    reduce(flippedMass, sumOp<scalar>());
    cumulativeFlipMass_ += flippedMass;

    if (nFlips > 0)
    {
        // Ym_ has changed, so the mass fractions the mixture density
        // is built from have to follow it - the same refresh the
        // bed collapse does after relocating mass.
        SolidState::deriveYiFromYm(Ym_, Ys_);
    }
}

void PorosityRecovery::markBedBorderCells()
{
    cellOnBedBorder_= cellOnBedBorder_*0;
    porosity_.correctBoundaryConditions();
    solidState_.correctSolidPresentBoundaryConditions();

    const volScalarField& solidPresent = solidState_.solidPresent();
    surfaceScalarField solidPresenceAtFaces = fvc::interpolate(solidPresent);
    forAll(solidPresenceAtFaces,faceI)
    {
        if ( (solidPresenceAtFaces[faceI] > 0) and (solidPresenceAtFaces[faceI] < 1) )
        {
            if (solidPresent[mesh_.owner()[faceI]] == 1)
            {
                cellOnBedBorder_[mesh_.owner()[faceI]] = 1;
            }
            if (solidPresent[mesh_.neighbour()[faceI]] == 1)
            {
                cellOnBedBorder_[mesh_.neighbour()[faceI]] = 1;
            }
        }
    }
    forAll(solidPresenceAtFaces.boundaryField(),patchI)
    {
        if (isA<processorPolyPatch>(mesh_.boundaryMesh()[patchI]))
        {
            const label start = mesh_.boundaryMesh()[patchI].start();
            forAll(solidPresenceAtFaces.boundaryField()[patchI],faceI)
            {
                if ( (solidPresenceAtFaces.boundaryField()[patchI][faceI] > 0) and (solidPresenceAtFaces.boundaryField()[patchI][faceI] < 1) )
                {
                    if (solidPresent[mesh_.owner()[faceI + start]] == 1)
                    {
                        cellOnBedBorder_[mesh_.owner()[faceI + start]] = 1;
                    }
                }
            }
        }
    }
}


// * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * * //

void PorosityRecovery::recover()
{
    if (!active_)
    {
        return;
    }

    porositySource_ = chemistry_.RRpor(T_)();

    const volScalarField totalYm(SolidState::totalMass(Ym_));

    porosityFromSolidMass(totalYm);

    label nFlips = 0;
    const labelList flipCells(cellsToEmpty(totalYm, nFlips));
    const List<Field<label>> procFlipList(gatherFlipLists(flipCells));

    if (bedCollapse_.active())
    {
        if (bedCollapse_.collapseOnto(procFlipList))
        {
            // Bed motion moved Ym_ between cells; rebuild the mass
            // fractions Ys_ from the relocated mass concentrations.
            SolidState::deriveYiFromYm(Ym_, Ys_);
        }
    }
    else if (emptyFlippedCells_)
    {
        emptyCells(flipCells, nFlips);
    }

    solidState_.refreshSolidPresent();

    // Here, after everything above that writes porosity_ directly: the
    // bed collapse and the emptying of flipped cells.
    guardPorosity
    (
        "the bed motion in PorosityRecovery::recover()",
        porosity_,
        phiSolid_
    );
    solidStateChecker_->checkConsistency(porosity_, Ym_, rho_);

    markBedBorderCells();

    // porosity_ has just been rewritten; the gate is derived from it.
    solidState_.freezeConnectedAtStepStart();

    // On the mass the bed motion left, for the written regime record.
    solidState_.classify(SolidState::totalMass(Ym_)(), rho_);
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// ************************************************************************* //
