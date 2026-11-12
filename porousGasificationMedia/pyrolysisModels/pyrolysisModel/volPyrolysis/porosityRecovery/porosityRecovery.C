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
    const SolidFields& solid,
    volScalarField& cellOnBedBorder,
    const SolidDiagnostics& solidDiagnostics,
    const Switch& infoOutput
)
:
    mesh_(solid.mesh),
    porosity_(solid.porosity),
    T_(solid.T),
    rho_(solid.rho),
    Ym_(solid.Ym),
    Ys_(solid.Ys),
    solidH_(solid.solidH),
    Us_(solid.Us),
    phiSolid_(solid.phiSolid),
    chemistry_(solid.chemistry),
    cellOnBedBorder_(cellOnBedBorder),
    solidState_(solid.state),
    solidDiagnostics_(solidDiagnostics),
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
{
    Info << "poroProtectSolidInflowFluxTolerance  "
         << poroProtectSolidInflowFluxTolerance_ << " [kg/m3/s]" << endl;
    Info << "emptyFlippedCells        " << emptyFlippedCells_ << endl;
}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

PorosityRecovery::~PorosityRecovery()
{}


// * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * * //

void PorosityRecovery::updatePorosity()
{
    porositySource_ = chemistry_.RRpor(T_)();

    const volScalarField totalYm(SolidState::totalMass(Ym_));

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
    guardPorosity("the porosity recovery in PorosityRecovery::updatePorosity()");

    Info<< "porosity recovered from solid mass. Chemistry source (not"
        << " applied) min/max   = " << gMin(porositySource_)
        << ", " << gMax(porositySource_);

    Info<< "; values min Y = " << gMin(por)
        <<" max Y = " << gMax(por) << endl;
}

FlipCells PorosityRecovery::findFlipCells() const
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

    // Do not empty a cell while solid is still filling it through the
    // advective transport equation (an advancing edge).
    const volScalarField totalYm(SolidState::totalMass(Ym_));
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

    FlipCells flips;
    flips.local = labelList(flipStack);
    flips.nGlobal = flipStack.size();

    reduce(nCandidates, sumOp<label>());
    reduce(nProtected, sumOp<label>());
    reduce(flips.nGlobal, sumOp<label>());

    if (infoOutput_ && Pstream::master() && nCandidates > 0)
    {
        Info<< "solid flip guard: candidates=" << nCandidates
            << " vetoed(incoming-solid)=" << nProtected
            << " flipped=" << flips.nGlobal
            << endl;
    }

    flips.allRanks.setSize(Pstream::nProcs());
    flips.allRanks[Pstream::myProcNo()] = flips.local;
    Pstream::gatherList(flips.allRanks);
    Pstream::scatterList(flips.allRanks);

    return flips;
}

void PorosityRecovery::emptyCells(const FlipCells& flips)
{
    // Emptying a cell removes its solid outright rather than just
    // rewriting porosity; the mass is destroyed (no destination) and
    // charged to cumulativeFlipMass_. T_ zeroed to match solidH_.
    const scalarField& V = mesh_.V();
    scalar flippedMass = 0.0;

    forAll(flips.local,entI)
    {
        const label cellI = flips.local[entI];

        forAll(Ym_, i)
        {
            flippedMass += Ym_[i][cellI]*V[cellI];
            Ym_[i][cellI] = 0.0;
        }

        solidH_[cellI] = 0.0;
        T_[cellI] = 0.0;
        porosity_[cellI] = 1.0;
    }

    reduce(flippedMass, sumOp<scalar>());
    cumulativeFlipMass_ += flippedMass;

    if (flips.nGlobal > 0)
    {
        // Ym_ has changed, so the mass fractions the mixture density
        // is built from have to follow it - the same refresh the
        // bed collapse does after relocating mass.
        SolidState::deriveYiFromYm(Ym_, Ys_);
    }
}

void PorosityRecovery::guardPorosity(const word& stage) const
{
    const label badCell = solidDiagnostics_.firstInvalidPorosity(porosity_);

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
        << "    div(phiUs)   = " << fvc::div(phiSolid_)()[badCell] << nl
        << "    sum(Ym)      = " << sumYm << nl
        << "    rho          = " << rho_[badCell] << nl
        << "    solidPresent = " << solidState_.solidPresent()[badCell];

    solidDiagnostics_.abort(stage, porosity_, badCell, context.str());
}

void PorosityRecovery::markBedBorderCells()
{
    cellOnBedBorder_= cellOnBedBorder_*0;

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


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// ************************************************************************* //
