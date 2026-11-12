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

void volPyrolysis::readControls()
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
        readControls();
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
        readControls();
        return true;
    }
    else
    {
        return false;
    }
}

void volPyrolysis::limitSolidFlux()
{
    // The Ym patches lag the bed motion and the porosity patches
    // updateSolidState() rewrote since the species step set them.
    if (solidFluxLimiter_->limiting())
    {
        forAll(Ym_, i)
        {
            Ym_[i].correctBoundaryConditions();
        }
    }

    phiSolid_ = solidFluxLimiter_->limit();
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

void volPyrolysis::createCollaborators()
{
    Info << endl;

    const SolidFields solid
    {
        mesh_,
        solidThermo_,
        solidChemistry_(),
        solidState_(),
        T_,
        rho_,
        Ys_,
        Ym_,
        solidH_(),
        porosity_,
        Us_,
        phiSolid_,
        active_
    };

    solidDiagnostics_.reset(new SolidDiagnostics(coeffs(), solid));
    solidDiagnostics_->checkBoundaryDeclarations(Ys_, T_);

    solidFluxLimiter_.reset(new SolidFluxLimiter(coeffs(), solid));

    bedCollapse_.reset
    (
        new BedCollapse(coeffs(), solid, porosityArch_)
    );

    heatExchange_.reset
    (
        new GasSolidHeatExchange
        (
            coeffs(),
            solid,
            porosityArch_,
            gasThermo_,
            equilibrium_
        )
    );

    porosityRecovery_.reset
    (
        new PorosityRecovery
        (
            coeffs(),
            solid,
            cellOnBedBorder_,
            solidDiagnostics_(),
            infoOutput_
        )
    );

    // The radiation model reads the flags before the first step. Not
    // porosity_'s patches: evaluating a coded inlet here would change step 1.
    solidState_->correctSolidPresentBoundaryConditions();
    porosityRecovery_->markBedBorderCells();

    solidEnergy_.reset
    (
        new SolidEnergy
        (
            solid,
            gasThermo_,
            radiation_,
            chemistrySh_,
            heatExchange_(),
            solidDiagnostics_(),
            equilibrium_
        )
    );

    demCoupling_.reset
    (
        new DemCoupling(mesh_, solidThermo_.composition())
    );

    speciesTransport_.reset
    (
        new SolidSpeciesTransport(solid, demCoupling_(), solidDiagnostics_())
    );

    Info << endl;
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
    solidThermo_(solidThermo),
    solidChemistry_
    (
        porousThermoSolidChemistryModel<HGSSolidThermo>::New
        (
            solidThermo_,
            gasThermo_.composition().Y(),
            gasThermo.thermoName()
        )
    ),
    rho_(solidThermo_.rho()),
    Ys_(solidThermo_.composition().Y()),
    Ym_(Ys_.size()),
    T_(solidThermo_.T()),
    solidH_(nullptr),
    equilibrium_(false),
    nNonOrthCorr_(-1),
    maxDiff_(10),
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
    solidState_(new SolidState(coeffs(), porosity_)),
    heatExchange_(nullptr),
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
    solidDiagnostics_(nullptr),
    chemistryTimeStep_(1.0)
{
    mesh.setFluxRequired(T_.name());

    specieTransferCoeff_ = specieTransfer_->ST()();

    createYm();

    createSolidH();

    solidState_->classifyInitialState();

    // The radiation model is constructed next and holds a reference to the
    // gate, so it has to be valid before the first correct() rather than
    // after the first porosity recovery.
    solidState_->freezeConnectedAtStepStart();

    solidState_->classify(SolidState::totalMass(Ym_)(), rho_);

    // porosity_ is assigned by porosityRecovery_, not solved; storeOldTimes()
    // is a no-op on the first call, so seed oldTime() here or the gas-side
    // fvm::ddt(porosityF, rho) loses a step of d(porosity)/dt.
    porosity_.oldTime();

    renormaliseInitialComposition();

    if (active_)
    {
        read();
    }

    createCollaborators();
}

// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

volPyrolysis::~volPyrolysis()
{}


// * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * * //

scalar volPyrolysis::solidRegionDiffNo() const
{
    scalar NumCprho = 0.0;
    scalar DiNum = 0.0;

    if (mesh_.nInternalFaces() > 0)
    {
        surfaceScalarField KrhoCpbyDelta
        (
            mesh_.surfaceInterpolation::deltaCoeffs()
          * fvc::interpolate(K())
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

    chemistrySh_ = solidChemistry_->Sh()();
}

void volPyrolysis::exchangeHeatWithGas()
{
    // The same per-species sources rhoEqn and YEqn read, so the energy
    // crossing the interface moves with exactly the mass that crosses it.
    PtrList<volScalarField> SrhoI(gasThermo_.composition().Y().size());
    forAll(SrhoI, i)
    {
        SrhoI.set(i, Srho(i).ptr());
    }
    heatExchange_->updateInterfaceEnthalpy(SrhoI);

    if (active_ && !equilibrium_)
    {
        heatExchange_->updateHeatTransfer();
    }
}

void volPyrolysis::moveBed()
{
    const FlipCells flips(porosityRecovery_->findFlipCells());

    // After the flip cells: they read the gradient of last step's mask.
    solidState_->refreshSolidPresent();

    if (bedCollapse_->active())
    {
        // Collapse moves Ym between cells, so the mass fractions follow
        if (bedCollapse_->collapseOnto(flips.allRanks))
        {
            SolidState::deriveYiFromYm(Ym_, Ys_);
        }
    }
    else if (porosityRecovery_->emptyFlippedCells())
    {
        porosityRecovery_->emptyCells(flips);
    }

    // Before checkPorosity(), whose reports print solidPresent
    solidState_->refreshSolidPresent();
}

void volPyrolysis::checkPorosity() const
{
    porosityRecovery_->guardPorosity("the bed motion in volPyrolysis::moveBed()");
    solidDiagnostics_->checkConsistency(porosity_, Ym_, rho_);
}

void volPyrolysis::updateSolidState()
{
    porosity_.correctBoundaryConditions();
    solidState_->correctSolidPresentBoundaryConditions();
    porosityRecovery_->markBedBorderCells();

    // porosity_ has just been rewritten; the gate is derived from it.
    solidState_->freezeConnectedAtStepStart();

    // On the mass the bed motion left, for the written regime record.
    solidState_->classify(SolidState::totalMass(Ym_)(), rho_);
}

void volPyrolysis::reportStep()
{
    solidDiagnostics_->reportMassBudget
    (
        speciesTransport_->clippedMass(),
        porosityRecovery_->flippedMass(),
        bedCollapse_->collapsedMass()
    );
    solidDiagnostics_->reportDerivedSpecies();

    if (infoOutput_)
    {
        solidDiagnostics_->accumulateRunTotals(chemistrySh_);
    }
    info();
}

tmp<volScalarField> volPyrolysis::zeroSrho(const word& name) const
{
    return tmp<volScalarField>
    (
        new volScalarField
        (
            IOobject
            (
                name,
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

void volPyrolysis::evolveRegion()
{
    // Each stage reads what the ones above it left; the order sets results.

    // Rates and exchange, from the step-start state
    markReactingCells();
    updateTransferCoefficients();
    solveChemistry();
    exchangeHeatWithGas();

    // Settle how much solid each face may carry before any solid field is
    // transported. Judge the limited flux, the one that carries Ym and
    // solidH, before either moves.
    limitSolidFlux();
    solidDiagnostics_->checkSolidCrossing(phiSolid_, Ys_, Ym_);

    // Species before energy: the enthalpy balance is closed on the mass
    // that remains, with the fluxes that moved it.
    speciesTransport_->solve();
    solidEnergy_->solve(speciesTransport_->specieMassFlux());

    // The DEM temperature term differences this step's Ts.
    demCoupling_->addTemperatureTerm();
    demCoupling_->finishStep();

    // Porosity and bed
    porosityRecovery_->updatePorosity();  // 1 - porosity = sum_i Ym_i/rho_i
    moveBed();                            // collapse onto flip cells, or empty them
    checkPorosity();
    updateSolidState();                   // masks, bed border, next step's gate

    reportStep();
}

Foam::tmp<Foam::volScalarField> volPyrolysis::Srho() const
{
    tmp<volScalarField> tSrho(zeroSrho("volPyrolysis::Srho"));

    if (active_)
    {
        forAll(solidChemistry_->gasTable(), gasI)
        {
            tSrho.ref() += solidChemistry_->RRg(gasI);
        }
    }

    return tSrho;
}


Foam::tmp<Foam::volScalarField> volPyrolysis::Srho(const label i) const
{
    const word& gasName = gasThermo_.composition().Y()[i].name();
    const label gasI = solidChemistry_->gasTable().find(gasName);

    if (active_ && gasI != -1)
    {
        return solidChemistry_->RRg(gasI);
    }

    return zeroSrho("volPyrolysis::Srho(" + Foam::name(i) + ")");
}

void volPyrolysis::info() const
{
    Info<< "\nPyrolysis: " << endl;

    solidDiagnostics_->reportRunTotals(bedCollapse_->replenishedMass());

    if (chemistryTimeStep_ < GREAT)
    {
        Info<< indent << "Suggested chemical time step from heterogeneous reactions [s] = "
            << chemistryTimeStep_ << nl;
    }
}

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam
} // End namespace heterogeneousPyrolysisModels

// ************************************************************************* //
