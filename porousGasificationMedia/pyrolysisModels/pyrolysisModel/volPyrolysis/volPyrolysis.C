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

#include "volFields.H"
#include "volPyrolysis.H"
#include "dimensionSet.H"
#include "addToRunTimeSelectionTable.H"
#include "mapDistribute.H"
#include "zeroGradientFvPatchFields.H"
#include "surfaceInterpolate.H"
#include "fvm.H"
#include "fvcDiv.H"
#include "fvcVolumeIntegrate.H"
#include "fvMatrices.H"
#include "fvCFD.H"
#include "DynamicList.H"
#include "processorPolyPatch.H"
#include "processorCyclicPolyPatch.H"
#include "upwind.H"

#include "BCs/fixedSolidH/fixedSolidHFvPatchScalarField.H"
#include "BCs/fixedYm/fixedYmFvPatchScalarField.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

defineTypeNameAndDebug(volPyrolysis, 0);

addToRunTimeSelectionTable(heterogeneousPyrolysisModel, volPyrolysis, radiation);

// * * * * * * * * * * * * Protected Member Functions  * * * * * * * * * * * //

void volPyrolysis::readReactingOneDimControls()
{
    const dictionary& solution = this->solution().subDict("PIMPLE");
    solution.lookup("nNonOrthogonalCorrectors") >> nNonOrthCorr_;
    time_.controlDict().lookup("maxDi") >> maxDiff_;
    equilibrium_.readIfPresent("equilibrium",coeffs_);
}

bool volPyrolysis::read()
{
    if (heterogeneousPyrolysisModel::read())
    {
        readReactingOneDimControls();
        return true;
    }
    else
    {
        return false;
    }
}

bool volPyrolysis::read(const dictionary& dict)
{
    if (heterogeneousPyrolysisModel::read(dict))
    {
        readReactingOneDimControls();
        return true;
    }
    else
    {
        return false;
    }
}

void volPyrolysis::deriveYiFromYm()
{
    // rho_ = 1/sum_i(Ys_i/rho_i) is a density only where sum_i Ys_i = 1;
    // a fixedYm patch beside an empty cell injects at that density.
    forAll(Ym_[0], cellI)
    {
        scalar Ysum = 0.0;
        forAll(Ys_, i)
        {
            Ysum += Ym_[i][cellI];
        }

        if (Ysum > SMALL)
        {
            forAll(Ys_, i)
            {
                Ys_[i][cellI] = Ym_[i][cellI] / Ysum;
            }
        }
        else
        {
            // The cell carries no mass, so nothing it holds depends on the
            // choice - only a fixedYm patch beside it reads this density.
            forAll(Ys_, i)
            {
                Ys_[i][cellI] = 0.0;
            }
            Ys_[0][cellI] = 1.0;
        }
    }
}

void volPyrolysis::limitSolidVolFlux()
{
    phiSolid_ = solidFluxLimiter_->limit();
}

void volPyrolysis::guardPorosity
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
        << "    maxSolidFraction = " << solidState_->maxSolidFraction() << nl
        << "    Us           = " << Us_[badCell] << nl
        << "    div(phiUs)   = " << fvc::div(phiUs)()[badCell] << nl
        << "    sum(Ym)      = " << sumYm << nl
        << "    rho          = " << rho_[badCell] << nl
        << "    solidPresent = " << solidState_->solidPresent()[badCell];

    solidStateChecker_->abort(stage, por, badCell, context.str());
}

void volPyrolysis::porosityFromSolidMass(const volScalarField& totalYm)
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
        "the porosity recovery in recoverPorosity()",
        por,
        phiSolid_
    );

    Info<< "porosity recovered from solid mass. Chemistry source (not"
        << " applied) min/max   = " << gMin(porositySource_)
        << ", " << gMax(porositySource_);

    Info<< "; values min Y = " << gMin(por)
        <<" max Y = " << gMax(por) << endl;
}

labelList volPyrolysis::cellsToEmpty
(
    const volScalarField& totalYm,
    label& nFlips
)
{
    FIFOStack<label> candidateStack = {};

    volVectorField solidPresenceGrad = fvc::grad(solidState_->solidPresent());

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
            solidState_->isDispersedOrTrace(cellI)
         && (interior || recedingEdge)
        )
        {
            candidateStack.push(cellI);
        }
    }

    // After the candidates: they read the gradient of last step's mask.
    solidState_->refreshSolidPresent();

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

List<Field<label>> volPyrolysis::gatherFlipLists
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

void volPyrolysis::emptyCells(const labelList& flipCells, const label nFlips)
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
        deriveYiFromYm();
    }
}

void volPyrolysis::markBedBorderCells()
{
    cellOnBedBorder_= cellOnBedBorder_*0;
    porosity_.correctBoundaryConditions();
    solidState_->correctSolidPresentBoundaryConditions();

    const volScalarField& solidPresent = solidState_->solidPresent();
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

void volPyrolysis::recoverPorosity()
{
    if (!active_)
    {
        return;
    }

    porositySource_ = solidChemistry_->RRpor(T_)();

    const volScalarField totalYm(totalSolidMass());

    porosityFromSolidMass(totalYm);

    label nFlips = 0;
    const labelList flipCells(cellsToEmpty(totalYm, nFlips));
    const List<Field<label>> procFlipList(gatherFlipLists(flipCells));

    if (bedCollapse_->active())
    {
        if (bedCollapse_->collapseOnto(procFlipList))
        {
            // Bed motion moved Ym_ between cells; rebuild the mass
            // fractions Ys_ from the relocated mass concentrations.
            deriveYiFromYm();
        }
    }
    else if (emptyFlippedCells_)
    {
        emptyCells(flipCells, nFlips);
    }

    solidState_->refreshSolidPresent();

    // Here, after everything above that writes porosity_ directly: the
    // bed collapse and the emptying of flipped cells.
    guardPorosity
    (
        "the bed motion in recoverPorosity()",
        porosity_,
        phiSolid_
    );
    solidStateChecker_->checkConsistency(porosity_, Ym_, rho_);

    markBedBorderCells();

    // porosity_ has just been rewritten; the gate is derived from it.
    solidState_->freezeConnectedAtStepStart();

    // On the mass the bed motion left, for the written regime record.
    solidState_->classify(totalSolidMass()(), rho_);
}

void volPyrolysis::beginLambdaDotStep()
{
#ifdef WITH_YADE
    if (demActive_)
    {
        lamDotCalc_->beginStep();
        lamDotCalc_->calculateTemperatureDriven();
    }
#endif
}

void volPyrolysis::addChemistryLambdaDot
(
    const volScalarField& sRhoSi,
    const label i
)
{
    // A no-op under lambdaMode constant. The name is the bare
    // solidComponents one ("char", not "Ychar"), as dlambdaOverDYmi is keyed.
#ifdef WITH_YADE
    if (demActive_)
    {
        lamDotCalc_->calculateChemistryDriven
        (
            sRhoSi,
            solidThermo_.composition().components()[i]
        );
    }
#endif
}

void volPyrolysis::checkSpecieMass
(
    const label i,
    const volScalarField& divYmFlux,
    const volScalarField& sRhoSi
) const
{
    const label badCell =
        solidStateChecker_->firstInvalidExtensive(Ym_[i]);

    if (badCell != -1)
    {
        OStringStream context;
        context
            << "    specie       = " << Ys_[i].name() << nl
            << "    Us           = " << Us_[badCell] << nl
            << "    div(phiUs Ym)= " << divYmFlux[badCell] << nl
            << "    RRs          = " << sRhoSi[badCell] << nl
            << "    porosity     = " << porosity_[badCell];

        solidStateChecker_->abort
        (
            "the solid specie mass equation in solveSpeciesMass()",
            Ym_[i],
            badCell,
            context.str()
        );
    }
}

void volPyrolysis::clipNegativeSpecieMass(volScalarField& Ym_i)
{
    cumulativeYmClip_ +=
        gSum(max(-Ym_i.field(), 0.0)*mesh_.V());

    Ym_i.max(0.0);                       // mass concentration >= 0
}

void volPyrolysis::solveSpecieMass(const label i)
{
    volScalarField& Ym_i = Ym_[i];
    volScalarField sRhoSi = solidChemistry_->RRs(i);

    // lambdaDot only reads sRhoSi, which the Ym equation uses unchanged.
    addChemistryLambdaDot(sRhoSi, i);

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

void volPyrolysis::finishLambdaDotStep()
{
#ifdef WITH_YADE
    if (demActive_)
    {
        lambdaDotPtr_->correctBoundaryConditions();
    }
#endif
}

void volPyrolysis::reportSolidMassBudget() const
{
    scalar totalYmMass = 0.0;
    for (label i = 0; i < Ym_.size(); ++i)
    {
        totalYmMass += gSum(Ym_[i].field() * mesh_.V());
    }

    Info<< "solid mass budget: sum(Ym_i * V) = " << totalYmMass
        << ", initial = " << initialTotalYmMass_
        << ", fabricated by clip = " << cumulativeYmClip_
        << ", destroyed by flip = " << cumulativeFlipMass_
        << ", destroyed by collapse = " << bedCollapse_->collapsedMass()
        << endl;
}

void volPyrolysis::reportDerivedSpecies() const
{
    for (label i = 0; i < Ys_.size(); ++i)
    {
        Info<< "       derived " << Ys_[i].name()
            << " min/max = " << gMin(Ys_[i]) << " / " << gMax(Ys_[i]) << endl;
    }
}

void volPyrolysis::solveSpeciesMass()
{
// eqZx2uHGn045
// eqZx2uHGn046

    if (debug)
    {
        Info<< "volPyrolysis:+solveSpeciesMass()" << endl;
    }

    if (!active_)
    {
        return;
    }

    // Reset lambdaDot and add its temperature term; the chemistry
    // term accumulates per specie in solveSpecieMass().
    beginLambdaDotStep();

    for (label i = 0; i < Ys_.size(); ++i)
    {
        solveSpecieMass(i);
    }

    deriveYiFromYm();

    // lambdaDot is complete once every specie has contributed.
    finishLambdaDotStep();

    reportSolidMassBudget();
    reportDerivedSpecies();
}

Foam::tmp<Foam::volScalarField> volPyrolysis::totalSolidMass() const
{
    tmp<volScalarField> tTotalYm(new volScalarField(0*Ym_[0]));
    forAll(Ym_, i)
    {
        tTotalYm.ref() += Ym_[i];
    }
    return tTotalYm;
}

Foam::tmp<Foam::volScalarField> volPyrolysis::solidHeatCapacity
(
    const volScalarField& totalYm
) const
{
    return solidState_->withSolidHeatCapacityFloor(totalYm*solidThermo_.Cp());
}

Foam::tmp<Foam::volScalarField> volPyrolysis::preReactionMass
(
    const volScalarField& totalYm
) const
{
    return max
    (
        totalYm - time_.deltaT()*solidChemistry_->RRs()(),
        dimensionedScalar("zero", dimDensity, 0.0)
    );
}

Foam::tmp<Foam::volScalarField> volPyrolysis::preReactionHeatCapacity
(
    const volScalarField& preReactionYm
)
{
    volScalarField rhoCp(preReactionYm*solidThermo_.Cp());

    // Cp() is sum_i Ys_i*Cp_i, so with Ys_ holding the pre-reaction masses
    // instead of fractions it returns sum_i Ym_i*Cp_i. Ys_ is restored after.
    List<scalarField> postReactionYs(Ys_.size());
    forAll(Ys_, i)
    {
        postReactionYs[i] = Ys_[i].primitiveField();
        Ys_[i].primitiveFieldRef() = max
        (
            Ym_[i].primitiveField()
          - time_.deltaTValue()*solidChemistry_->RRs(i)().primitiveField(),
            scalar(0)
        );
    }

    const scalarField exactRhoCp(solidThermo_.Cp()().primitiveField());

    forAll(Ys_, i)
    {
        Ys_[i].primitiveFieldRef() = postReactionYs[i];
    }

    // preReactionYm*Cp() pairs the old mass with the new composition,
    // scaling Ts by Cp_pre/Cp_post each step. Below SMALL, Ys_ is
    // deriveYiFromYm()'s placeholder, so those cells keep it.
    forAll(rhoCp, cellI)
    {
        if (preReactionYm[cellI] > SMALL)
        {
            rhoCp[cellI] = exactRhoCp[cellI];
        }
    }

    return solidState_->withSolidHeatCapacityFloor(rhoCp);
}

void volPyrolysis::recoverTsFromH
(
    const volScalarField& rhoCp,
    const volScalarField& totalYm,
    const bool dispersedRule
)
{
    if (dispersedRule)
    {
        T_ = solidState_->withDispersedRule(solidH_()/rhoCp, gasThermo_.T());
        T_ = solidState_->withTraceRule(T_, totalYm, rho_, gasThermo_.T());
    }
    else
    {
        T_ = solidState_->withTraceRule
        (
            solidH_()/rhoCp,
            totalYm,
            rho_,
            gasThermo_.T()
        );
    }
}

void volPyrolysis::storeH(const volScalarField& rhoCp)
{
    solidH_().ref() = rhoCp*T_;
    solidH_().correctBoundaryConditions();
}

Foam::tmp<Foam::fvScalarMatrix> volPyrolysis::conductionWithinSolid
(
    const volTensorField& composedK
)
{
    const surfaceScalarField solidPresenceAtFaces
    (
        fvc::interpolate(solidState_->solidPresent())
    );

    // Simplistic immersed boundary for heat transport in solid phase.
    tmp<fvScalarMatrix> tTLap(new fvScalarMatrix(fvm::laplacian(composedK, T_)));
    fvScalarMatrix& TLap = tTLap.ref();

    // Setting face fluxes on the border of porous media to 0.
    // this should be instead flux due to the macroscopic motion to smooth at the edges
    forAll(solidPresenceAtFaces,faceI)
    {
       if ( (solidPresenceAtFaces[faceI] > 0) and (solidPresenceAtFaces[faceI] < 1) )
       {
           TLap.upper()[faceI] = 0.;
       }
    }
    forAll(solidPresenceAtFaces.boundaryField(),patchI)
    {
       if (isA<processorPolyPatch>(mesh_.boundaryMesh()[patchI]) or isA<cyclicPolyPatch>(mesh_.boundaryMesh()[patchI]) or isA<processorCyclicPolyPatch>(mesh_.boundaryMesh()[patchI]) )
       {
           forAll(solidPresenceAtFaces.boundaryField()[patchI],faceI)
           {
               if ( (solidPresenceAtFaces.boundaryField()[patchI][faceI] > 0) and (solidPresenceAtFaces.boundaryField()[patchI][faceI] < 1) )
               {
                   TLap.boundaryCoeffs()[patchI][faceI] = 0;
                   TLap.internalCoeffs()[patchI][faceI] = 0;
               }
           }
       }
    }

    TLap.diag() = 0;
    TLap.negSumDiag();
    // Exact on orthogonal meshes. On non-orthogonal ones, zeroing the source
    // drops the correction from div(composedK): spurious at the solid edge,
    // negligible inside the bed in relevant cases.
    TLap.source() = 0.;

    return tTLap;
}

void volPyrolysis::solveTsEquation
(
    const volTensorField& composedK,
    const volScalarField& rhoCp,
    const volScalarField& solidBeyondTrace
)
{
    const volScalarField& connectedAtStepStart = solidState_->connectedAtStepStart();

    // Evaluated once and stored, so the gas EEqn adds back the same
    // joules after TEqn has moved T_. heatTransferCalc() gates
    // itself; gating again here would be a no-op, not a safeguard.
    heatTransferSh_ = heatTransferCalc()();
    const volScalarField& interphaseHeatTransfer = heatTransferSh_;

    // radiationSh_ carries no connected gate of its own: as a cell
    // empties, rhoCp collapses toward its SMALL floor while the T^4
    // radiative sink stays full strength.
    volScalarField solidRadiationSource = connectedAtStepStart*radiationSh_;

    // Same collapsing-rhoCp gap as solidRadiationSource, for the
    // reaction heat: chemistrySh_ is set from RRs_/RRg_ (nonzero
    // wherever there is solid) with no connected gate of its own.
    volScalarField solidChemistrySource = connectedAtStepStart*chemistrySh_;

    const tmp<fvScalarMatrix> tTLap(conductionWithinSolid(composedK));

    fvScalarMatrix TEqn
    (
        fvm::ddt(rhoCp,T_)
      - tTLap()
    ==
        solidChemistrySource // eqZx2uHGn004, eqZx2uHGn017
      - interphaseHeatTransfer // eqZx2uHGn005
      - heatUpGas_
      + solidRadiationSource
    );

    TEqn.relax();
    TEqn.solve();

    const label badTEqnCell =
        solidStateChecker_->firstInvalidTemperature
        (
            T_,
            solidBeyondTrace.primitiveField()
        );

    if (badTEqnCell != -1)
    {
        OStringStream context;
        context
            << "    rhoCp        = " << rhoCp[badTEqnCell] << nl
            << "    chemistrySh  = "
            << solidChemistrySource[badTEqnCell] << nl
            << "    heatTransfer = "
            << interphaseHeatTransfer[badTEqnCell] << nl
            << "    heatUpGas    = " << heatUpGas_[badTEqnCell] << nl
            << "    radiationSh  = "
            << solidRadiationSource[badTEqnCell] << nl
            << "    porosity     = " << porosity_[badTEqnCell];

        solidStateChecker_->abort
        (
            "the solid energy equation in solveTsEquation()",
            T_,
            badTEqnCell,
            context.str()
        );
    }
}

void volPyrolysis::advectSolidH
(
    const volScalarField& rhoCp,
    const volScalarField& totalYm
)
{
    const surfaceScalarField& solidFlux = phiSolid_;

    // Enthalpy rides the solid MASS flux, not phiSolid: div(phiSolid)
    // is nonlinear, so it would give solidH and Ym different face
    // values. phiYm here matches solveSpeciesMass()'s own flux.
    surfaceScalarField phiYm
    (
        fvc::flux(solidFlux, Ym_[0], "div(phiSolid)")
    );
    for (label i = 1; i < Ym_.size(); ++i)
    {
        phiYm += fvc::flux(solidFlux, Ym_[i], "div(phiSolid)");
    }

    // hs upwinds on the mass flux: a receiver ends up holding the
    // convex combination (solidH + m_in*hs_donor)/(totalYm + m_in).
    // Cp*T_, not solidH/totalYm, to match rhoCp's own floor.
    volScalarField hs(solidThermo_.Cp()*T_);

    dimensionedScalar ovDt = pow(time_.deltaT(),-1);
    fvScalarMatrix sHEqn
    (
        fvm::Sp(ovDt,solidH_()) - ovDt*solidH_()
      + fvc::surfaceIntegrate
        (
            phiYm*upwind<scalar>(mesh_, phiYm).interpolate(hs)
        )
    );

    sHEqn.relax();
    sHEqn.solve();

    const volScalarField& sH = solidH_();

    const label badSolidHCell =
        solidStateChecker_->firstInvalidExtensive(sH);

    if (badSolidHCell != -1)
    {
        OStringStream context;
        context
            << "    Us           = " << Us_[badSolidHCell] << nl
            << "    Ts           = " << T_[badSolidHCell] << nl
            << "    rhoCp        = " << rhoCp[badSolidHCell] << nl
            << "    sum(Ym)      = " << totalYm[badSolidHCell] << nl
            << "    porosity     = " << porosity_[badSolidHCell];

        solidStateChecker_->abort
        (
            "the solid enthalpy advection in advectSolidH()",
            sH,
            badSolidHCell,
            context.str()
        );
    }

    solidH_().max(0);
    solidH_().correctBoundaryConditions();
}

void volPyrolysis::reportSolidTemperatureRange() const
{
    scalar minTemp = GREAT;
    scalar maxTemp = -GREAT;
    scalar areThere = 0;
    forAll(T_,cellI)
    {
        if (solidState_->solidPresent()[cellI] == 1 )
        {
            areThere = 1;
            if (T_[cellI] < minTemp)
            {
                minTemp = T_[cellI];
            }
            if (T_[cellI] > maxTemp)
            {
                maxTemp = T_[cellI];
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

void volPyrolysis::solveSolidEnergy()
{
// eqZx2uHGn047

    if (debug)
    {
        Info<< "volPyrolysis::solveSolidEnergy()" << endl;
    }

    if (!active_)
    {
        return;
    }

    radiationSh_ = radiation_;

    if (equilibrium_)
    {
        return;
    }

    const volTensorField composedK(K_ * solidState_->solidFraction() * anisotropyK_);

    forAll(Ym_, i)
    {
        Ym_[i].correctBoundaryConditions();
    }

    const volScalarField totalYm(totalSolidMass());

    // What the two Ts guards below are judged on.
    const volScalarField solidBeyondTrace
    (
        solidState_->massBeyondTrace(totalYm, rho_)
    );

    // The heat capacity of the solid the cell actually holds. No
    // solidPresent factor: solidH_ carries none either, and
    // masking one and not the other is a temperature of any size.
    const volScalarField rhoCp(solidHeatCapacity(totalYm));

    // Only the trace rule: the dispersed rule was applied at the end
    // of the last step, and solidH_ was restated from that Ts.
    recoverTsFromH(rhoCp, totalYm, false);

    const label badTsCell =
        solidStateChecker_->firstInvalidTemperature
        (
            T_,
            solidBeyondTrace.primitiveField()
        );

    if (badTsCell != -1)
    {
        OStringStream context;
        context
            << "    solidH       = "
            << solidH_()[badTsCell] << nl
            << "    sum(Ym)      = " << totalYm[badTsCell] << nl
            << "    rhoCp        = " << rhoCp[badTsCell] << nl
            << "    porosity     = " << porosity_[badTsCell] << nl
            << "    solidPresent = " << solidState_->solidPresent()[badTsCell];

        solidStateChecker_->abort
        (
            "Ts = solidH/rhoCp at the head of solveSolidEnergy()",
            T_,
            badTsCell,
            context.str()
        );
    }

    T_.correctBoundaryConditions();
    solidState_->correctSolidPresentBoundaryConditions();

    solveTsEquation(composedK, rhoCp, solidBeyondTrace);
    storeH(rhoCp);
    advectSolidH(rhoCp, totalYm);
}

void volPyrolysis::recoverTsAfterReaction()
{
// eqZx2uHGn047

    if (debug)
    {
        Info<< "volPyrolysis::recoverTsAfterReaction()" << endl;
    }

    if (!active_ || equilibrium_)
    {
        return;
    }

    const volScalarField totalYm(totalSolidMass());
    solidThermo_.correct();

    const volScalarField rhoCp(solidHeatCapacity(totalYm));

    // The mass solveSpeciesMass() has just converted to gas. Ym_
    // carries the loss, solidH_ does not: it was built in
    // solveSolidEnergy() on what the cell held before the reaction.
    const volScalarField preReactionYm(preReactionMass(totalYm));

    // Divided by the mass that carried the enthalpy: gas-bound solid takes
    // its sensible enthalpy with it, so losing mass must not raise Ts. No
    // solidPresent factor: it lags a stage, and would start the cell at 0 K.
    const volScalarField rhoCpPreReaction(preReactionHeatCapacity(preReactionYm));
    recoverTsFromH(rhoCpPreReaction, totalYm, true);

    const label badCell =
        solidStateChecker_->firstInvalidTemperature(T_);

    if (badCell != -1)
    {
        OStringStream context;
        context
            << "    solidH       = "
            << solidH_()[badCell] << nl
            << "    sum(Ym)      = " << totalYm[badCell] << nl
            << "    sum(Ym) pre  = " << preReactionYm[badCell] << nl
            << "    rhoCp        = " << rhoCp[badCell] << nl
            << "    porosity     = " << porosity_[badCell] << nl
            << "    solidPresent = " << solidState_->solidPresent()[badCell];

        solidStateChecker_->abort
        (
            "the Ts recovery in recoverTsAfterReaction()",
            T_,
            badCell,
            context.str()
        );
    }

    // Restated on the mass that remains, so solveSolidEnergy() reads
    // back this Ts next step instead of re-inflating it.
    storeH(rhoCp);
    T_.correctBoundaryConditions();

    reportSolidTemperatureRange();
}

void volPyrolysis::calculateMassTransfer()
{
    if (infoOutput_)
    {
        totalGasMassFlux_ = fvc::domainIntegrate(solidChemistry_->RRg());
        totalHeatRR_ = fvc::domainIntegrate(chemistrySh_);

        addedGasMass_ +=
            fvc::domainIntegrate(solidChemistry_->RRg()) * time_.deltaT();
        lostSolidMass_ +=
            fvc::domainIntegrate(solidChemistry_->RRs()) * time_.deltaT();
    }
}

const SolidState& volPyrolysis::solidState() const
{
    return solidState_();
}

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

volPyrolysis::volPyrolysis
(
    const word& modelType,
    const fvMesh& mesh,
    HGSSolidThermo& solidThermo,
    psiReactionThermo& gasThermo,
    volScalarField& porosity,
    volScalarField& radiation
)
:
    heterogeneousPyrolysisModel(modelType, mesh),
    porosity_(porosity),
    porosityArch_
    (
        IOobject
        (
            "porosityF0",
            time_.timeName(),
            mesh_,
            IOobject::MUST_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_
    ),
    STmodel_(specieTransferModel::New(porosity_,porosityArch_)),
    ST_
    (
        IOobject
        (
            "STvol",
            time_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero",dimless/dimTime, 0.0)
    ),
    gasThermo_(gasThermo),
    Ygas_(gasThermo.composition().Y()),
    solidThermo_(solidThermo),
    solidChemistry_
    (
        porousThermoSolidChemistryModel<HGSSolidThermo>::New
        (
            solidThermo_,
            Ygas_,
            gasThermo.thermoName()
        )
    ),
    kappa_(solidThermo_.kappa()),
    K_(solidThermo_.K()),
    rho_(solidThermo_.rho()),
    rho0_
    (
        IOobject
        (
            "rhos0",
            time_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimMass/dimVolume, 0.0)
    ),
    Ys_(solidThermo_.composition().Y()),
    Ym_(Ys_.size()),
    T_(solidThermo_.T()),
    solidH_(nullptr),
    equilibrium_(false),
    subintegrateSwitch_(false),
    emptyFlippedCells_(false),
    poroProtectSolidInflowFluxTolerance_(1e-12),
    nNonOrthCorr_(-1),
    maxDiff_(10),
    porosity0_(porosity),
    gasVolumeFractionAtStepStart_(porosity),
    radiation_(radiation),
    chemistrySh_
    (
        IOobject
        (
            "solidChemistrySh",
            time_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimEnergy/dimTime/dimVolume, 0.0)
    ),
    porositySource_
    (
        IOobject
        (
            "porosityS",
            time_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimless/dimTime, 0.0)
    ),
    solidState_(new SolidState(coeffs(), porosity_)),
    cellVolume_
    (
        IOobject
        (
            "cellVolume",
            time_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimless, 0.0)
    ),
    heatUpGas_
    (
        IOobject
        (
            "heatUpGas",
            time_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimEnergy/dimTime/dimVolume, 0.0)
    ),
    heatTransferSh_
    (
        IOobject
        (
            "pyrolysisSh",
            time_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimEnergy/dimTime/dimVolume, 0.0)
    ),
    HTmodel_(porousHeatTransferModel::New(porosity_,porosityArch_)),
    CONV_
    (
        IOobject
        (
            "CONVvol",
            time_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero",dimEnergy/dimTime/dimTemperature/dimVolume , 0.0)
    ),
    radiationSh_
    (
        IOobject
        (
            "radiationSh",
            time_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimEnergy/dimVolume/dimTime, 0.0)
    ),
    anisotropyK_
    (
        IOobject
        (
            "anisotropyK",
            time_.timeName(),
            mesh_,
            IOobject::READ_IF_PRESENT,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedTensor("one", dimless, tensor(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0))
    ),
    cellOnBedBorder_
    (
        IOobject
        (
            "cellOnBedBorder",
            time_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimless, 0.0)
    ),
    // Bind to the single registered "Us" field created by the solver
    // (createFields.H), rather than owning a duplicate. lambdaDotModel
    // writes that same object when DEM coupling is active.
    Us_
    (
        mesh_.lookupObject<volVectorField>("Us")
    ),
    phiSolid_
    (
        IOobject
        (
            "phiSolid",
            time_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimVolume/dimTime, 0.0)
    ),
    solidFluxLimiter_(nullptr),
    solidStateChecker_(nullptr),
    demActive_(false),
    lambdaDotPtr_(nullptr),
    lostSolidMass_(dimensionedScalar("zero", dimMass, 0.0)),
    addedGasMass_(dimensionedScalar("zero", dimMass, 0.0)),
    totalGasMassFlux_(dimensionedScalar("zero", dimMass/dimTime, 0.0)),
    totalHeatRR_(dimensionedScalar("zero", dimEnergy/dimTime, 0.0)),
    timeChem_(1.0),
    initialTotalYmMass_(0.0),
    cumulativeYmClip_(0.0),
    cumulativeFlipMass_(0.0)
{

    mesh.setFluxRequired(T_.name());

    ST_ = STmodel_->ST()();
    CONV_ = CONV();
    rho0_.ref() = rho_.ref();

    subintegrateSwitch_ = coeffs().lookupOrDefault("subintegrateHeatTransfer",false);
    bedCollapse_.reset
    (
        new BedCollapse
        (
            coeffs(),
            mesh_,
            porosity_,
            porosityArch_,
            T_,
            rho_,
            Ym_,
            Ys_,
            solidH_,
            cellVolume_,
            solidState_()
        )
    );
    emptyFlippedCells_ =
        coeffs().lookupOrDefault("emptyFlippedCells",false);
    poroProtectSolidInflowFluxTolerance_ =
        coeffs().lookupOrDefault
        (
            "poroProtectSolidInflowFluxTolerance",
            1e-12
        );

    Info << endl;
    Info << "subintegrateHeatTransfer " << subintegrateSwitch_ << endl;
    Info << "bedCollapse              " << bedCollapse_->active()    << endl;
    Info << "poroProtectSolidInflowFluxTolerance  "
         << poroProtectSolidInflowFluxTolerance_
         << " [kg/m3/s]" << endl;
    Info << "replenish                " << bedCollapse_->replenish()    << endl;
    Info << "collapseMovesSolidMass   " << bedCollapse_->movesSolidMass() << endl;
    Info << "emptyFlippedCells        " << emptyFlippedCells_  << endl;
    // Constructed here because it carries the last banner lines.
    solidStateChecker_.reset
    (
        new SolidStateChecker(coeffs(), solidState_(), mesh_, time_)
    );

    // Reads its own keys and logs them where the rest are reported. Binds
    // Ym_ by reference, so it reads no entries until limit() is first called.
    solidFluxLimiter_.reset
    (
        new SolidFluxLimiter
        (
            coeffs(),
            mesh_,
            Us_,
            Ym_,
            rho_,
            T_,
            solidChemistry_(),
            active_,
            solidState_()
        )
    );

    Info << endl;

    forAll(Ys_, fieldI)
    {
          // Where the user set a fixedValue condition on Yi, the Ym patch
          // becomes fixedYm, which keeps Ym = Yi*rho*(1-porosity) current
          // as rho and porosity evolve at the boundary (mirrors the
          // fixedSolidH pattern for T/solidH). All other patch types are
          // inherited from Yi as-is.
          const volScalarField::Boundary& ybf = Ys_[fieldI].boundaryField();
          wordList ymTypes(ybf.types());
          forAll(ybf, patchi)
          {
              if (isA<fixedValueFvPatchScalarField>(ybf[patchi]))
              {
                  ymTypes[patchi] = fixedYmFvPatchScalarField::typeName;
              }
          }

          Ym_.set
          (
                fieldI,
                new volScalarField
                (
                    IOobject
                    (
                        Ys_[fieldI].name() + "m",
                        time_.timeName(),
                        mesh_,
                        IOobject::NO_READ,
                        IOobject::AUTO_WRITE
                    ),
                    mesh_,
                    dimensionedScalar("zero",dimMass/dimVolume,0.0),
                    ymTypes
                )
          );
          // Ym_i = Yi * rho_s * (1 - porosity): mass per unit total volume.
          Ym_[fieldI].ref() =
              Ys_[fieldI].ref() * rho_.ref() * (1.0 - porosity_.ref());

          // Set initial boundary values from Yi, scaled to Ym units;
          // updateCoeffs() keeps the fixedYm patches current thereafter.
          forAll(Ym_[fieldI].boundaryField(), patchI)
          {
              Ym_[fieldI].boundaryFieldRef()[patchI] ==
                  Ys_[fieldI].boundaryField()[patchI]
                * rho_.boundaryField()[patchI]
                * (1.0 - porosity_.boundaryField()[patchI]);
          }

          initialTotalYmMass_ += gSum(Ym_[fieldI].field() * mesh_.V());
    }

    const volScalarField::Boundary& tbf = T_.boundaryField();
    wordList tbt(tbf.types());

    forAll(tbf, patchi)
    {
        if (isA<fixedValueFvPatchScalarField>(tbf[patchi]))
        {
            tbt[patchi] = fixedSolidHFvPatchScalarField::typeName;
        }
    }

    solidH_.reset
    (
        new volScalarField
        (
            IOobject
            (
                "solidH",
                time_.timeName(),
                mesh_,
                IOobject::READ_IF_PRESENT,
                IOobject::AUTO_WRITE
            ),
            mesh,
            dimensionSet(1, -1, -2, 0, 0),
            tbt
        )
    );

    solidH_() = rho_ * solidThermo_.Cp() * (1 - porosity_) * T_;

    forAll(tbf, patchi)
    {
        if (isA<cyclicFvPatch>(mesh_.boundary()[patchi]))
        {
            if (isA<fixedValueFvPatchScalarField>(tbf[patchi]))
            {
                tmp<fvPatchScalarField> tPatch
                (
                    fvPatchScalarField::New
                    (
                        fixedSolidHFvPatchScalarField::typeName,
                        cyclicPolyPatch::typeName,
                        mesh.boundary()[patchi],
                        solidH_().internalField()
                    )
                ); 
                solidH_().boundaryFieldRef().set(patchi, tPatch);
            }
            if (isA<zeroGradientFvPatchScalarField>(tbf[patchi]))
            {
                tmp<fvPatchScalarField> tPatch
                (
                    fvPatchScalarField::New
                    (
                        zeroGradientFvPatchScalarField::typeName,
                        cyclicPolyPatch::typeName,
                        mesh.boundary()[patchi],
                        solidH_().internalField()
                    )
                ); 
                solidH_().boundaryFieldRef().set(patchi, tPatch);
            }
        }
    }

    solidH_().correctBoundaryConditions();

    solidState_->classifyInitialState();

    // The radiation model is constructed next and holds a reference to the
    // gate, so it has to be valid before the first correct() rather than
    // after the first recoverPorosity().
    solidState_->freezeConnectedAtStepStart();

    solidState_->classify(totalSolidMass()(), rho_);

    // porosity_ is assigned in recoverPorosity(), not solved; storeOldTimes()
    // is a no-op on the first call, so seed oldTime() here or the gas-side
    // fvm::ddt(porosityF, rho) loses a step of d(porosity)/dt.
    porosity_.oldTime();

    forAll(rho_,cellI)
    {
        if (porosity_[cellI] == 1.)
        {
             rho0_[cellI] = 3.14;
        }
    }

    // Initial renormalization via Ym: rebuild Ym from the read-in Yi, then
    // re-derive Yi = Ym / sum(Ym) so the two representations are mutually
    // consistent from the first time step.
    forAll(rho_,cellI)
    {
        if (solidState_->solidPresent()[cellI] == 1)
        {
            scalar Ytotal = 0.0;
            for (label i = 0; i < Ys_.size(); ++i)
            {
                Ym_[i][cellI] =
                    Ys_[i][cellI] * rho_[cellI] * (1.0 - porosity_[cellI]);
                Ytotal += Ym_[i][cellI];
            }
            if (Ytotal > SMALL)
            {
                for (label i = 0; i < Ys_.size(); ++i)
                {
                    Ys_[i][cellI] = Ym_[i][cellI] / Ytotal;
                }
            }
        }
    }

    if (active_)
    {
        read();
    }

    forAll(solidState_->solidPresent(), cellI)
    {
        if (solidState_->solidPresent()[cellI] == 1)
        {
            bool surfC = false;
            forAll(mesh_.cellCells()[cellI],cellJ)
            {
                if (solidState_->solidPresent()[mesh_.cellCells()[cellI][cellJ]] == 0)
                {
                    surfC = true;
                }
            }
            if (surfC) cellOnBedBorder_[cellI] = 1;
        }
    }

    forAll(cellVolume_,cellI)
    {
        cellVolume_[cellI] = mesh_.V()[cellI];
    }
    cellVolume_.correctBoundaryConditions();

    // DEM counts as active only when both signals agree: yadeProperties asks
    // for it, and the lambdaDot/lambda fields exist. Either alone misleads --
    // a WITH_YADE solver registers the fields even with DEM off, and a
    // non-YADE solver can still read active=true.
    {
        IOdictionary yadeProperties
        (
            IOobject
            (
                "yadeProperties",
                time_.constant(),
                mesh_,
                IOobject::READ_IF_PRESENT,
                IOobject::NO_WRITE,
                false // unregistered: the solver may own this object name
            )
        );

        const Switch demRequested
        (
            yadeProperties.lookupOrDefault<Switch>("active", false)
        );

        const bool demFieldsRegistered =
            mesh_.foundObject<volScalarField>("lambdaDot")
         && mesh_.foundObject<volScalarField>("lambda");

        demActive_ = demRequested && demFieldsRegistered;

        if (demActive_)
        {
            lambdaDotPtr_ =
                &mesh_.lookupObjectRef<volScalarField>("lambdaDot");

#ifdef WITH_YADE
            // lambdaMode and its coefficients. Opened unregistered, as
            // lambdaDotModel reads the same dictionary for its interpolation
            // entries.
            IOdictionary lambdaDict
            (
                IOobject
                (
                    "lambdaDict",
                    time_.constant(),
                    mesh_,
                    IOobject::MUST_READ,
                    IOobject::NO_WRITE,
                    false
                )
            );

            lamDotCalc_ = LambdaDotCalculationModel::New
            (
                lambdaDict, mesh_, *lambdaDotPtr_
            );
#endif
        }
        else if (demRequested)
        {
            WarningInFunction
                << "yadeProperties requests DEM coupling (active=true) but "
                << "the solver did not register the lambdaDot/lambda fields "
                << "(built without WITH_YADE?); "
                << "lambdaDot calculation disabled" << endl;
        }

        Info<< "volPyrolysis: DEM lambdaDot calculation "
            << (demActive_ ? "active" : "inactive") << endl;
    }
}

// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

volPyrolysis::~volPyrolysis()
{}


// * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * * //
volScalarField& volPyrolysis::rhoConst() const
{
    return rho_;
}

volScalarField& volPyrolysis::rho()
{
    return rho_;
}

const volScalarField& volPyrolysis::T() const
{
    return T_;
}

const tmp<volScalarField> volPyrolysis::Cp() const
{
    return solidThermo_.Cp();
}


const volScalarField& volPyrolysis::kappa() const
{
    return kappa_;
}

const volScalarField& volPyrolysis::K() const
{
    return K_;
}

const volScalarField& volPyrolysis::surf() const
{
    return cellOnBedBorder_;
}

scalar volPyrolysis::maxDiff() const
{
    return maxDiff_;
}

scalar volPyrolysis::solidRegionDiffNo() const
{
    scalar NumCprho = 0.0;
    scalar DiNum = 0.0;

    if (mesh_.nInternalFaces() > 0)
    {
        surfaceScalarField KrhoCpbyDelta
        (
            mesh_.surfaceInterpolation::deltaCoeffs()
          * fvc::interpolate(K_)
        );

        surfaceScalarField Cprho
        (
            fvc::interpolate(Cp()*rho_)
        );

        NumCprho = max(Cprho.ref()).value();
        reduce(NumCprho, maxOp<scalar>());
        if (NumCprho != 0.)
        {
            DiNum = gMax(KrhoCpbyDelta.internalField())*time_.deltaTValue()/NumCprho;
        }
        else
        {
            DiNum = SMALL;
        }
    }

    return DiNum;
}

scalar volPyrolysis::maxTime() const
{
return timeChem_;
}

Switch volPyrolysis::equilibrium() const
{
    return equilibrium_;
}

void volPyrolysis::preEvolveRegion() {
    // Added for completeness
    heterogeneousPyrolysisModel::preEvolveRegion();

    // Iterates over every cell and sets cells whose solid energy equation
    // will accept the reaction heat as reacting. A cell whose solid is not
    // connected converts no mass, so none of its heat goes unbooked.
    forAll(T_, cellI)
    {
        if ( active_ && solidState_->connectedAtStepStart()[cellI] != 0)
        {
            solidChemistry_->setCellReacting(cellI, true);
        }
        else
        {
            solidChemistry_->setCellReacting(cellI, false);
        }
    }

}

void volPyrolysis::evolveRegion()
{
    gasVolumeFractionAtStepStart_ = porosity_;

    if (!equilibrium_)
    {
        CONV_ = CONV();
    }

    ST_ = STmodel_->ST()();

    timeChem_ = solidChemistry_->solve
    (
        time_.value() - time_.deltaTValue(),
        time_.deltaTValue()
    );

    chemistrySh_ = solidChemistry_->Sh()(); // eqZx2uHGn004
    heatUpGas_ = heatUpGasCalc()();

    // Settle how much solid each face may carry before any solid field is
    // transported. Needs the chemistry rates, so it follows their solve.
    limitSolidVolFlux();

    // Energy before species: solidH_ holds the pre-reaction solid until
    // recoverTsAfterReaction() restates it on the mass that remains.
    solveSolidEnergy();
    solveSpeciesMass();
    recoverTsAfterReaction();

    recoverPorosity();

    calculateMassTransfer();
    info();
}


Foam::tmp<Foam::volScalarField> volPyrolysis::Srho() const
{
    Foam::tmp<Foam::volScalarField> tSrho
    (
        new volScalarField
        (
            IOobject
            (
                "thermoSingleLayer::Srho",
                time_.timeName(),
                mesh_,
                IOobject::NO_READ,
                IOobject::AUTO_WRITE,
                false
            ),
            mesh_,
            dimensionedScalar("zero", dimMass/dimVolume/dimTime, 0.0)
        )
    );

    if (active_)
    {
        const speciesTable& gasTable = solidChemistry_->gasTable();

        forAll(gasTable,gasI)
        {
            tSrho = tSrho + solidChemistry_->RRg(gasI);
        }
    }

    return tSrho;
}


Foam::tmp<Foam::volScalarField> volPyrolysis::Srho(const label i) const
{

    if (active_)
    {
        const speciesTable& gasTable = solidChemistry_->gasTable();

        label j = -1;
        forAll(gasTable,gasI)
        {
            if (gasTable[gasI] == Ygas_[i].name())
            {
                j = gasI;
            }
        }

        if (j > -1)
        {

            tmp<volScalarField> tRRiGas = solidChemistry_->RRg(j);
            return tRRiGas;
        }
        else
        {
            return Foam::tmp<Foam::volScalarField>
            (
                new volScalarField
                (
                    IOobject
                    (
                        "volPyrolysis::Srho(" + Foam::name(i) + ")",
                        time_.timeName(),
                        mesh_,
                        IOobject::NO_READ,
                        IOobject::NO_WRITE,
                        false
                    ),
                    mesh_,
                    dimensionedScalar("zero", dimMass/dimVolume/dimTime, 0.0)
                )
            );

        }
    }
    else
    {
        return Foam::tmp<Foam::volScalarField>
        (
            new volScalarField
            (
                IOobject
                (
                    "volPyrolysis::Srho(" + Foam::name(i) + ")",
                    time_.timeName(),
                    mesh_,
                    IOobject::NO_READ,
                    IOobject::NO_WRITE,
                    false
                ),
                mesh_,
                dimensionedScalar("zero", dimMass/dimVolume/dimTime, 0.0)
            )
        );
    }
}

Foam::tmp<Foam::volScalarField> volPyrolysis::heatTransferCalc() const
{
// eqZx2uHGn005
    Foam::tmp<Foam::volScalarField> Sh_ = Foam::tmp<Foam::volScalarField>
    (
        new volScalarField
        (
            IOobject
            (
                "pyrolysisSh",
                time_.timeName(),
                mesh_,
                IOobject::NO_READ,
                IOobject::AUTO_WRITE,
                false
            ),
            mesh_,
            dimensionedScalar("zero", dimEnergy/dimVolume/dimTime, 0.0)
        )
    );

    if (!equilibrium_)
    {
        // Gate the emitted exchange at the producer so the solid TEqn and the
        // gas EEqn consume the same field: an ungated gas side would exchange
        // heat with a solid whose own equation refuses it, creating energy.
        const volScalarField& connectedAtStepStart = solidState_->connectedAtStepStart();

        if (subintegrateSwitch_)
        {
            // Keep the subintegrated gas/solid exchange defined in a fully
            // solid cell. The physical gas capacity vanishes there, while
            // the finite floor prevents an ill-conditioned analytic update.
            volScalarField rhoCpG
            (
                gasThermo_.rho()
              * gasThermo_.Cp()
              * solidState_->gasFractionWithFloor()
            );
            volScalarField Tgas = gasThermo_.T();
            volScalarField rhoCpS
            (
                solidState_->withSolidHeatCapacityFloor
                (
                    rho_ * solidThermo_.Cp() * solidState_->solidFraction()
                )
            );
            volScalarField deltaTemp(T_ * 0);
            scalar deltaTime = time_.deltaTValue();

            forAll(deltaTemp,cellI)
            {
                if (solidState_->solidPresent()[cellI] == 1.0 && CONV_[cellI] > 0.0)
                {
                    deltaTemp[cellI] =
                        (Tgas[cellI]
                         * (rhoCpG[cellI]
                           +rhoCpS[cellI]
                           *exp(-(CONV_[cellI] * deltaTime*(rhoCpS[cellI]+rhoCpG[cellI]))
                                /(rhoCpS[cellI]*rhoCpG[cellI])))
                           +rhoCpS[cellI]*T_[cellI]
                           *(1 - exp( -(CONV_[cellI]*deltaTime*(rhoCpS[cellI]+rhoCpG[cellI]))/(rhoCpS[cellI]*rhoCpG[cellI])))
                        )
                        / (rhoCpS[cellI]+rhoCpG[cellI]) - Tgas[cellI];
                }
                else
                {
                    deltaTemp[cellI] = 0;
                }
            }

            forAll(Sh_(),cellI)
            {
                Sh_.ref()[cellI] = deltaTemp[cellI] * rhoCpG[cellI] * connectedAtStepStart[cellI] / deltaTime;
            }

            volScalarField HT(CONV_*(T_-Tgas));
            Info << "The heat transfer subintegration info:" << nl
                 << " no subintegration min, max:" << gMin(HT) << " " << gMax(HT) << nl
                 << " subintegration    min, max:" << gMin(Sh_()) << " " << gMax(Sh_()) << endl;
        }
        else
        {
            // This works only for small CONV otherwise oscillations appear.
            volScalarField Tgas = gasThermo_.T();
            volScalarField HT(CONV_*(T_-Tgas));
            Sh_ = HT * connectedAtStepStart;
        }
    }
    return Sh_;
}

Foam::tmp<Foam::volScalarField> volPyrolysis::CONV() const
{
    Foam::tmp<Foam::volScalarField> CONVloc_ = Foam::tmp<Foam::volScalarField>
    (
        new volScalarField
        (
            IOobject
            (
                "CONV",
                time_.timeName(),
                mesh_,
                IOobject::NO_READ,
                IOobject::AUTO_WRITE,
                false
            ),
            mesh_,
            dimensionedScalar("zero", dimEnergy/dimTemperature/dimTime/dimVolume, 0.0)
        )
    );

    if (!equilibrium_)
    {
        CONVloc_ = HTmodel_->CONV()*solidState_->solidPresent();
    }
    return CONVloc_;
}

Foam::tmp<Foam::volScalarField> volPyrolysis::heatUpGasCalc() const
{

    Foam::tmp<Foam::volScalarField> hSh_ = Foam::tmp<Foam::volScalarField>
    (
        new volScalarField
        (
            IOobject
            (
                "hSh_",
                time_.timeName(),
                mesh_,
                IOobject::NO_READ,
                IOobject::AUTO_WRITE,
                false
            ),
            mesh_,
            dimensionedScalar("zero", dimEnergy/dimVolume/dimTime, 0.0)
        )
    );

    if (active_)
    {
        volScalarField gasCp = gasThermo_.Cp();

        if (!equilibrium_) // eqZx2uHGn018
        {
            // Gated at the producer, so both consumers keep seeing one field.
            // The pair already conserved; what the gate adds is a bound, the
            // same one its three siblings in TEqn have.
            volScalarField tempSh = hSh_();
            tempSh = gasThermo_.Cp() * (T_ - gasThermo_.T()) * Srho();
            hSh_ = tempSh * solidState_->connectedAtStepStart() * solidState_->solidFraction();
        }
    }

    return hSh_;
}

Foam::tmp<Foam::volScalarField> volPyrolysis::heatUpGas() const
{
    return heatUpGas_;
}

Foam::tmp<Foam::volScalarField> volPyrolysis::heatTransfer() const
{
    return heatTransferSh_;
}

Foam::tmp<Foam::volScalarField> volPyrolysis::solidChemistrySh() const
{
    return chemistrySh_;
}

void volPyrolysis::info() const
{
    Info<< "\nPyrolysis: " << endl;

    Info<< indent << "Total gas mass produced  [kg] = " << addedGasMass_.value() << nl
        << indent << "Total solid mass lost    [kg] = " << lostSolidMass_.value() << nl
        << indent << "Total mass replenished   [kg] = " << bedCollapse_->replenishedMass() << nl
        << indent << "Realese rate of pyrolysis gases  [kg/s] = " << totalGasMassFlux_.value() << nl
        << indent << "Heat release rate [J/s] = " << totalHeatRR_.value() << nl;

        if (timeChem_ < GREAT )
        {
            Info << indent << "Suggested chemical time step from heterogeneous reactions [s] = "
                 << timeChem_ << nl;
        }
}
// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam
} // End namespace heterogeneousPyrolysisModels

// ************************************************************************* //
