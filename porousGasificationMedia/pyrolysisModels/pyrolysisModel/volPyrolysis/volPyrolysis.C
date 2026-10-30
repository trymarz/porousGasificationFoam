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

void volPyrolysis::limitSolidFlux()
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

    const volScalarField totalYm(SolidState::totalMass(Ym_));

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
    solidState_->classify(SolidState::totalMass(Ym_)(), rho_);
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
    lambdaDot_->addChemistryTerm
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
    lambdaDot_->beginStep();

    for (label i = 0; i < Ys_.size(); ++i)
    {
        solveSpecieMass(i);
    }

    deriveYiFromYm();

    // lambdaDot is complete once every specie has contributed.
    lambdaDot_->finishStep();

    solidStateChecker_->reportMassBudget
    (
        Ym_,
        initialTotalYmMass_,
        cumulativeYmClip_,
        cumulativeFlipMass_,
        bedCollapse_->collapsedMass()
    );
    solidStateChecker_->reportDerivedSpecies(Ys_);
}

void volPyrolysis::accumulateMassBudget()
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

void volPyrolysis::createYm()
{
    forAll(Ys_, fieldI)
    {
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
}

void volPyrolysis::createSolidH()
{
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
            mesh_,
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
                        mesh_.boundary()[patchi],
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
                        mesh_.boundary()[patchi],
                        solidH_().internalField()
                    )
                ); 
                solidH_().boundaryFieldRef().set(patchi, tPatch);
            }
        }
    }

    solidH_().correctBoundaryConditions();
}

void volPyrolysis::renormaliseInitialComposition()
{
    // So that Yi and Ym agree from the first time step.
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
}

void volPyrolysis::markInitialBedBorderCells()
{
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
    specieTransfer_(specieTransferModel::New(porosity_,porosityArch_)),
    specieTransferCoeff_
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
    heatExchange_
    (
        new GasSolidHeatExchange
        (
            coeffs(),
            mesh_,
            porosity_,
            porosityArch_,
            gasThermo_,
            solidThermo_,
            T_,
            rho_,
            solidState_(),
            equilibrium_,
            active_
        )
    ),
    solidEnergy_(nullptr),
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
    lostSolidMass_(dimensionedScalar("zero", dimMass, 0.0)),
    addedGasMass_(dimensionedScalar("zero", dimMass, 0.0)),
    totalGasMassFlux_(dimensionedScalar("zero", dimMass/dimTime, 0.0)),
    totalHeatRR_(dimensionedScalar("zero", dimEnergy/dimTime, 0.0)),
    chemistryTimeStep_(1.0),
    initialTotalYmMass_(0.0),
    cumulativeYmClip_(0.0),
    cumulativeFlipMass_(0.0)
{

    mesh.setFluxRequired(T_.name());

    specieTransferCoeff_ = specieTransfer_->ST()();
    heatExchange_->updateHeatTransferCoeff();
    rho0_.ref() = rho_.ref();

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
    Info << "subintegrateHeatTransfer " << heatExchange_->subintegrate() << endl;
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

    // Binds solidH_ by reference: it is created by createSolidH() below.
    solidEnergy_.reset
    (
        new SolidEnergy
        (
            mesh_,
            solidThermo_,
            gasThermo_,
            solidChemistry_(),
            Ym_,
            solidH_,
            porosity_,
            Us_,
            phiSolid_,
            radiation_,
            chemistrySh_,
            solidState_(),
            heatExchange_(),
            solidStateChecker_(),
            equilibrium_,
            active_
        )
    );

    Info << endl;

    createYm();

    createSolidH();

    solidState_->classifyInitialState();

    // The radiation model is constructed next and holds a reference to the
    // gate, so it has to be valid before the first correct() rather than
    // after the first recoverPorosity().
    solidState_->freezeConnectedAtStepStart();

    solidState_->classify(SolidState::totalMass(Ym_)(), rho_);

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

    renormaliseInitialComposition();

    if (active_)
    {
        read();
    }

    markInitialBedBorderCells();

    forAll(cellVolume_,cellI)
    {
        cellVolume_[cellI] = mesh_.V()[cellI];
    }
    cellVolume_.correctBoundaryConditions();

    // Last in the constructor, so its active/inactive line ends the banner.
    lambdaDot_.reset(new LambdaDotCoupling(mesh_));
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
return chemistryTimeStep_;
}

Switch volPyrolysis::equilibrium() const
{
    return equilibrium_;
}

void volPyrolysis::markReactingCells()
{
    // A cell whose solid is not connected converts no mass, so none of its
    // heat goes unbooked.
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

void volPyrolysis::updateTransferCoefficients()
{
    if (!equilibrium_)
    {
        heatExchange_->updateHeatTransferCoeff();
    }

    specieTransferCoeff_ = specieTransfer_->ST()();
}

void volPyrolysis::solveChemistry()
{
    chemistryTimeStep_ = solidChemistry_->solve
    (
        time_.value() - time_.deltaTValue(),
        time_.deltaTValue()
    );

    chemistrySh_ = solidChemistry_->Sh()(); // eqZx2uHGn004
}

void volPyrolysis::preEvolveRegion() {
    // Added for completeness
    heterogeneousPyrolysisModel::preEvolveRegion();

    markReactingCells();
}

void volPyrolysis::evolveRegion()
{
    gasVolumeFractionAtStepStart_ = porosity_;

    updateTransferCoefficients();
    solveChemistry();
    heatExchange_->updateHeatUpGas(Srho()());

    // Settle how much solid each face may carry before any solid field is
    // transported. Needs the chemistry rates, so it follows their solve.
    limitSolidFlux();

    // Energy before species: solidH_ holds the pre-reaction solid until
    // recoverTemperatureAfterReaction() restates it on the mass that remains.
    solidEnergy_->solve();
    solveSpeciesMass();
    solidEnergy_->recoverTemperatureAfterReaction();

    recoverPorosity();

    accumulateMassBudget();
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

Foam::tmp<Foam::volScalarField> volPyrolysis::heatUpGas() const
{
    return heatExchange_->heatUpGas();
}

Foam::tmp<Foam::volScalarField> volPyrolysis::heatTransfer() const
{
    return heatExchange_->heatTransfer();
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

        if (chemistryTimeStep_ < GREAT )
        {
            Info << indent << "Suggested chemical time step from heterogeneous reactions [s] = "
                 << chemistryTimeStep_ << nl;
        }
}
// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam
} // End namespace heterogeneousPyrolysisModels

// ************************************************************************* //
