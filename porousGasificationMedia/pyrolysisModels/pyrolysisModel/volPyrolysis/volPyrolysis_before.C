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

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

defineTypeNameAndDebug(volPyrolysis, 0);

addToRunTimeSelectionTable(heterogeneousPyrolysisModel, volPyrolysis, noRadiation);
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

void volPyrolysis::solveSpeciesMass()
{
// eqZx2uHGn045
// eqZx2uHGn046

    if (debug)
    {
        Info<< "volPyrolysis:+solveSpeciesMass()" << endl;
    }

    if (active_)
{
    volScalarField rhoLoc
    (
        max(rho_ * (1. - porosity_), dimensionedScalar("minRho", dimMass/dimVolume, SMALL))
    );

    surfaceScalarField solidPhi
    (
        IOobject
        (
            "solidPhi",
            time_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimMass/dimTime, 0.0)
    );

    surfaceScalarField solidVolFlux
    (
        IOobject
        (
            "solidVolFlux",
            time_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimVolume/dimTime, 0.0)
    );

    //DasteXar
 
    if (advectSolidFields_)
        {
            solidVolFlux = mesh_.Sf() & fvc::interpolate(Us_);
        }


    // if (advectSolidFields_)
    // {
    //     solidPhi = mesh_.Sf() & fvc::interpolate(rho_*Us_);
    //     solidVolFlux = mesh_.Sf() & fvc::interpolate(Us_);
    // }

    // fvScalarMatrix rhosEqn
    // (
    //     fvm::ddt(rho_)
    //  ==
    //   - fvc::div(solidPhi)
    // );

    // rhosEqn.relax();
    // rhosEqn.solve("rhos");

    for (label i = 0; i < Ym_.size(); ++i)
    {
        volScalarField& Ymi = Ym_[i];
        volScalarField sRhoSi = solidChemistry_->RRs(i);

        Ymi *= whereIs_;

        fvScalarMatrix YmEqn
        (
            fvm::ddt(Ymi)
          + fvm::div(solidVolFlux, Ymi, "div(phiSolid)")
         ==
            sRhoSi
        );

        YmEqn.relax();
        YmEqn.solve("Ym");

        Info<< "solid "<< Ym_[i].name()
            << " equation solved. Sources min/max   = " << gMin(sRhoSi)
            << ", " << gMax(sRhoSi)
            << "; values min Ym = " << gMin(Ym_[i])
            << " max Ym = " << gMax(Ym_[i]) << endl;
    }

    volScalarField Mt(0.0*Ym_[0]);

    for (label i = 0; i < Ym_.size(); ++i)
    {
        Mt += Ym_[i];
    }


    //DasteXar
            forAll(rho_, cellI)
        {
            const scalar alphaS = 1.0 - porosity_[cellI];

            if (porosity_[cellI] < critPorosity_ && alphaS > SMALL && Mt[cellI] > SMALL)
            {
                rho_[cellI] = Mt[cellI]/alphaS;
            }
        }

        rho_.correctBoundaryConditions();




    for (label i = 0; i < Ys_.size(); ++i)
    {
        Ys_[i] = whereIs_ * Ym_[i]
            / max(Mt, dimensionedScalar("minMass", Ym_[i].dimensions(), SMALL));

        Info<< "solid "<< Ys_[i].name()
            << " reconstructed from Ym; values min Y = " << gMin(Ys_[i])
            << " max Y = " << gMax(Ys_[i]) << endl;
    }
}

    // if (active_)
    // {
    //     volScalarField Mt(0.0 * Ym_[0]);
    //     volScalarField rhoLoc
    //     (
    //         max(rho_ * (1. - porosity_), dimensionedScalar("minRho",dimMass/dimVolume, SMALL))
    //     );

    //     surfaceScalarField solidPhi
    //     (
    //         IOobject
    //         (
    //             "solidPhi",
    //             time_.timeName(),
    //             mesh_,
    //             IOobject::NO_READ,
    //             IOobject::NO_WRITE
    //         ),
    //         mesh_,
    //         dimensionedScalar("zero", dimMass/dimTime, 0.0)
    //     );

    //     if (advectSolidFields_)
    //     {
    //         solidPhi = mesh_.Sf() & fvc::interpolate(rho_*Us_);
    //     }

    //     fvScalarMatrix rhosEqn
    //     (
    //         fvm::ddt(rho_)
    //      ==
    //       - fvc::div(solidPhi)
    //     );
    //     rhosEqn.relax();
    //     rhosEqn.solve("rhos");

    //     for (label i = 0; i < Ys_.size(); ++i)
    //     {

    //         volScalarField& Yi = Ys_[i];
    //         Yi.ref() *= whereIs_;
    //         volScalarField sRhoSi = solidChemistry_->RRs(i);

    //         surfaceScalarField solidFlux_
    //         (
    //             IOobject
    //             (
    //                 "solidFlux",
    //                 time_.timeName(),
    //                 mesh_,
    //                 IOobject::NO_READ,
    //                 IOobject::NO_WRITE
    //             ),
    //             mesh_,
    //             dimensionedScalar("zero", dimMass/dimTime, 0.0)
    //         );

    //         if (advectSolidFields_)
    //         {
    //             solidFlux_ = mesh_.Sf() & fvc::interpolate(Us_*rhoLoc);
    //         }

    //         // fvScalarMatrix YsEqn
    //         // (
    //         //     fvm::ddt(rhoLoc,Yi)
    //         //  ==
    //         //     sRhoSi
    //         //   - fvc::div(solidFlux_, Yi, "div(phiSolid)")
    //         // );

    //         YsEqn.relax();
    //         YsEqn.solve("Ys");

    //         Yi.max(0.0);
    //         Mt += Yi * rho_;

    //         Info<< "solid "<< Ys_[i].name()
    //             << " equation solved. Sources min/max   = " << gMin(sRhoSi)
    //             << ", " << gMax(sRhoSi)
    //             << "; values min Y = " << gMin(Ys_[i])
    //             << " max Y = " << gMax(Ys_[i]) << endl;
    //     }

    //     for (label i = 0; i < Ys_.size(); ++i)
    //     {
    //         Ym_[i] = whereIs_ * Ys_[i] * rho_;
    //         Ys_[i] = Ym_[i] / max(Mt,dimensionedScalar("minMass", dimMass/dimVolume, SMALL));
    //         Ym_[i] *= (1. - porosity_);
    //     }
    // }


}


void volPyrolysis::solveEnergy()
{
// eqZx2uHGn047

    if (debug)
    {
        Info<< "volPyrolysis::solveEnergy()" << endl;
    }

    if (active_)
    {

        volTensorField composedK(K_ * (1 - porosity_) * anisotropyK_);
        //radiationSh_ = radiation_;
        radiationSh_ = radiation_ * whereIs_; //DasteXar

        if (equilibrium_)
        {}
        else
        {
            
            // Ym_[i] is the transported and chemically updated solid mass
            // concentration (kg/m3) of total computational-cell volume.
            // Ym_[i] is the conserved quantity solved by 
                //fvm::ddt(Ymi)
                //+ fvm::div(solidVolFlux, Ymi)
        //therefore energy storage should use the same transported mass.

            // Therefore, sum(Ym_) is the most consistent quantity for calculatingthe volumetric solid heat capacity. 
            // Using rho_*(1-porosity_) here canbecome inconsistent with the mass actually transported by the Ym_ equations.
            
            volScalarField totalYm(0.0 * Ym_[0]);

            for (label i = 0; i < Ym_.size(); ++i)
            {
                totalYm += Ym_[i];
            }

            volScalarField rhoCp
            (
                max
                (
                    totalYm * solidThermo_.Cp(),
                    dimensionedScalar
                    (
                        "minRhoCp",
                        dimEnergy/dimTemperature/dimVolume,
                        SMALL
                    )
                )
            );




            volScalarField heatTransfField = heatTransfer()();

            // Solid volumetric face flux [m3/s].
            //
            // When advectSolidFields is disabled this remains zero. When it is
            // enabled, it uses exactly the same Eulerian solid velocity Us_ that
            // transports porosity and Ym_.
            surfaceScalarField solidVolFlux
            (
                IOobject
                (
                    "solidEnergyVolFlux",
                    time_.timeName(),
                    mesh_,
                    IOobject::NO_READ,
                    IOobject::NO_WRITE
                ),
                mesh_,
                dimensionedScalar("zero", dimVolume/dimTime, 0.0)
            );

            if (advectSolidFields_)
            {
                solidVolFlux = mesh_.Sf() & fvc::interpolate(Us_);
            }

            // Heat-capacity flux [J/(K s)].
            //
            // fvm::div(solidFluxRhoCp, T_) then transports the sensible energy
            // associated with the moving solid mass.
            surfaceScalarField solidFluxRhoCp
            (
                solidVolFlux * fvc::interpolate(rhoCp, "rhoCpInt")
            );

            whereIs_.correctBoundaryConditions();

            surfaceScalarField whereIsPatch
            (
                fvc::interpolate(whereIs_)
            );




           






            // Simplistic immersed boundary for heat transport in solid phase.
            fvScalarMatrix TLap
            (
                //fvm::laplacian(composedK, T_)
             // - fvc::div(solidFluxRhoCp,T_,"div(phiSolid)")
                //DasteXar 
             fvm::laplacian(composedK, T_)
            );
 
            // Setting face fluxes on the border of porous media to 0.
            // this should be instead flux due to the macrscopic motion to smooth at the edges
            forAll(whereIsPatch,faceI)
            {
               if ( (whereIsPatch[faceI] > 0) and (whereIsPatch[faceI] < 1) )
               {
                   TLap.upper()[faceI] = 0.;
               }
            }
            forAll(whereIsPatch.boundaryField(),patchI)
            {
               if (isA<processorPolyPatch>(mesh_.boundaryMesh()[patchI]))
               {
                   forAll(whereIsPatch.boundaryField()[patchI],faceI)
                   {
                       if ( (whereIsPatch.boundaryField()[patchI][faceI] > 0) and (whereIsPatch.boundaryField()[patchI][faceI] < 1) )
                       {
                           TLap.boundaryCoeffs()[patchI][faceI] = 0;
                           TLap.internalCoeffs()[patchI][faceI] = 0;
                       }
                   }
               }
            }

            TLap.diag() = 0;
            TLap.negSumDiag();
            // Correct on orthogonal meshes
            // For non-orthogona meshes
            // TLap.source() = 0. cancels the non-orthogonal correction from the
            // divergence of composedK, which is spurious on the edges of porous
            // media and negiligble inside porous media in relevant cases.
            TLap.source() = 0.;


            //DasteXar
            // fvScalarMatrix TEqn
            //     (
            //         fvm::ddt(rhoCp, T_)
            //     + fvm::div(solidFluxRhoCp, T_, "div(phiSolid)")
            //     - TLap
            //     ==
            //         chemistrySh_
            //     - heatTransfField
            //     - heatUpGas_
            //     + radiationSh_
            //     );

            //without this term, Ym_ and porosity move with Us_ but their thermal energy remains in the old cells.
            fvScalarMatrix TEqn
            (
                fvm::ddt(rhoCp, T_)

                // Transport sensible solid energy with the same solid velocity
                // used by the porosity and Ym_ transport equations.
            + fvm::div(solidFluxRhoCp, T_, "div(phiSolid)")

            - TLap
            ==

            // chemistrySh_ is calculated before evolvePorosity().
            //
            // If a cell crosses criticalPorosity during the current time step,
            // whereIs_ becomes zero and its Ym_ fields are removed. Therefore,
            // its heat capacity becomes almost zero. The already-calculated
            // chemistry source must also be disabled in that inactive cell;
            // otherwise Ts can increase to an unphysical value.
            whereIs_ * chemistrySh_


            //chemistrySh_       // Reaction heat source in the solid
            - heatTransfField    // Heat transferred from solid to gas
            - heatUpGas_         // Sensible heat carried by generated gas
            + radiationSh_       // Radiation received by the solid
            );



            TEqn.relax();
            TEqn.solve();

            T_.correctBoundaryConditions();

            // Recalculate rho, K, kappa and the other solid thermophysical
            // properties using the newly solved solid temperature.
       
            solidThermo_.correct();


        }

        scalar minTemp = GREAT;
        scalar maxTemp = -GREAT;
        scalar areThere = 0;

        forAll(T_,cellI)
        {
            if (whereIs_[cellI] == 1 )
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
}

void volPyrolysis::calculateMassTransfer()
{
    if (infoOutput_)
    {
        totalGasMassFlux_ = fvc::domainIntegrate(solidChemistry_->RRg());
        totalHeatRR_ = fvc::domainIntegrate(chemistrySh_);

        addedGasMass_ +=
            fvc::domainIntegrate(solidChemistry_->RRg()) * time_.deltaT();
        
            

        // RRs is normally negative when condensed-phase mass is consumed.
// Store the reported cumulative solid mass loss as a positive value.
lostSolidMass_ -=
    fvc::domainIntegrate(solidChemistry_->RRs()) * time_.deltaT(); }
}

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

volPyrolysis::volPyrolysis
(
    const word& modelType,
    const fvMesh& mesh,
    HGSSolidThermo& solidThermo,
    psiReactionThermo& gasThermo,
    volScalarField& whereIs
)
:
    heterogeneousPyrolysisModel(modelType, mesh),
    porosity_(whereIs),
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
    Msolid_(Ys_.size()),
    Msolidtotal_(
        IOobject
        (
            "Msolidtotal",
            time_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimMass, 0.0)
    ),
    T_(solidThermo_.T()),
    equilibrium_(false),
    subintegrateSwitch_(false),
    bedMotionSwitch_(false),
    advectSolidFields_(true),
    replenishSwitch_(false),
    critPorosity_(0.9999),
    totRepMass_(0.),
    nNonOrthCorr_(-1),
    maxDiff_(10),
    porosity0_(whereIs),
    voidFraction_(whereIs),
    radiation_(whereIs),
    phiHsGas_
    (
        IOobject
        (
            "phiHsGass",
            time_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimEnergy/dimTime, 0.0)
    ),
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
    whereIs_
    (
        IOobject
        (
            "whereIs",
            time_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("one", dimless, 1.0)
    ),
    whereIsNot_
    (
        IOobject
        (
            "whereIsNot",
            time_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimless, 0.0)
    ),
    whereWas_
    (
        IOobject
        (
            "whereWas",
            time_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimless, 0.0)
    ),
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
    HTmodel_(heatTransferModel::New(porosity_,porosityArch_)),
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
    surfF_(whereIs_),
    // Bind to the single registered "Us" field created by the solver
    // (createFields.H), rather than owning a duplicate. lambdaDotModel
    // writes that same object when DEM coupling is active.
    Us_
    (
        mesh_.lookupObject<volVectorField>("Us")
    ),
    lostSolidMass_(dimensionedScalar("zero", dimMass, 0.0)),
    addedGasMass_(dimensionedScalar("zero", dimMass, 0.0)),
    totalGasMassFlux_(dimensionedScalar("zero", dimMass/dimTime, 0.0)),
    totalHeatRR_(dimensionedScalar("zero", dimEnergy/dimTime, 0.0)),
    timeChem_(1.0)
{
    mesh.setFluxRequired(T_.name());

    ST_ = STmodel_->ST()();
    CONV_ = CONV();
    rho0_.ref() = rho_.ref();

    subintegrateSwitch_ = coeffs().lookupOrDefault("subintegrateHeatTransfer",false);
    bedMotionSwitch_ = coeffs().lookupOrDefault("bedCollapse",false);
    advectSolidFields_ = coeffs().lookupOrDefault("advectSolidFields", true);
    replenishSwitch_ = coeffs().lookupOrDefault("replenish",false);

    Info << endl;
    Info << "subintegrateHeatTransfer " << subintegrateSwitch_ << endl;
    Info << "bedCollapse              " << bedMotionSwitch_    << endl;
    Info << "advectSolidFields        " << advectSolidFields_  << endl;
    Info << "replenish                " << replenishSwitch_    << endl;
    Info << endl;

    forAll(rho_,cellI)
    {
        if (porosity_[cellI] == 1)
        {
             whereIsNot_[cellI] = 1;
             whereIs_[cellI] = 0;
             rho0_[cellI] = 3.14;
        }
    }

    forAll(Ys_, fieldI)
    {
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
                        dimensionedScalar("zero", dimMass/dimVolume, 0.0),
                        zeroGradientFvPatchScalarField::typeName
                    )
        );

        Ym_[fieldI] = Ys_[fieldI] * rho_ * (1.0 - porosity_); //DasteXar
        //Ym_[fieldI].ref() = Ys_[fieldI].ref() * rho_.ref();

        // Msolid is used for renormalization of mass fractions of solid species.
        Msolid_.set
        (
            fieldI,
            new volScalarField
            (
                IOobject
                (
                    Ys_[fieldI].name() + "m2",
                    time_.timeName(),
                    mesh_,
                    IOobject::NO_READ,
                    IOobject::AUTO_WRITE
                ),
                mesh_,
                dimensionedScalar("zero", dimMass, 0.0)
            )
        );
        forAll(rho_,cellI)
        {
            Msolid_[fieldI][cellI] =
                Ys_[fieldI][cellI]
               * rho_[cellI]
               * mesh_.V()[cellI]
               * (1 - porosity_[cellI]);
        }
    }

    if (active_)
    {
        read();
    }

    forAll(whereIs_,cellI)
    {
        if (whereIs_[cellI] == 1)
        {
            bool surfC = false;
            forAll(mesh_.cellCells()[cellI],cellJ)
            {
                    if (whereIs_[mesh_.cellCells()[cellI][cellJ]] == 0)
                    {
                        surfC = true;
                    }
            }
            if (surfC)
            {
                surfF_[cellI] = 1;
            }
        }
    }

    forAll(rho_,cellI)
    {
        Msolidtotal_[cellI] = 0.0;

        for (label i = 0; i < Ys_.size(); ++i)
        {
             Msolid_[i][cellI]=
                Ys_[i][cellI]
               *rho_[cellI]
               *mesh_.V()[cellI]
               *(1 - porosity_[cellI]);

             Msolidtotal_[cellI] += Msolid_[i][cellI];
        }

        for (label i = 0; i < Ys_.size(); ++i)
        {
            if(Msolidtotal_[cellI] > 0.0)
            {
               Ys_[i][cellI] = (Msolid_[i][cellI] / (Msolidtotal_[cellI]));
            }
        }
    }

    forAll(cellVolume_,cellI)
    {
        cellVolume_[cellI] = mesh_.V()[cellI];
    }
    cellVolume_.correctBoundaryConditions();
}

volPyrolysis::volPyrolysis
(
    const word& modelType,
    const fvMesh& mesh,
    HGSSolidThermo& solidThermo,
    psiReactionThermo& gasThermo,
    volScalarField& whereIs,
    volScalarField& radiation
)
:
    heterogeneousPyrolysisModel(modelType, mesh),
    porosity_(whereIs),
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
    Msolid_(Ys_.size()),
    Msolidtotal_(
        IOobject
        (
            "Msolidtotal",
            time_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimMass, 0.0)
    ),
    T_(solidThermo_.T()),
    equilibrium_(false),
    subintegrateSwitch_(false),
    bedMotionSwitch_(false),
    advectSolidFields_(true),
    replenishSwitch_(false),
    critPorosity_(0.9999),
    totRepMass_(0.),
    nNonOrthCorr_(-1),
    maxDiff_(10),
    porosity0_(whereIs),
    voidFraction_(whereIs),
    radiation_(radiation),
    phiHsGas_
    (
        IOobject
        (
            "phiHsGass",
            time_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimEnergy/dimTime, 0.0)
    ),
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
    whereIs_
    (
        IOobject
        (
            "whereIs",
            time_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimless, 0.0)
    ),
    whereIsNot_
    (
        IOobject
        (
            "whereIsNot",
            time_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedScalar("one", dimless, 1.0)
    ),
    whereWas_
    (
        IOobject
        (
            "whereWas",
            time_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimless, 0.0)
    ),
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
    HTmodel_(heatTransferModel::New(porosity_,porosityArch_)),
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
    surfF_
    (
        IOobject
        (
            "surfF",
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
    lostSolidMass_(dimensionedScalar("zero", dimMass, 0.0)),
    addedGasMass_(dimensionedScalar("zero", dimMass, 0.0)),
    totalGasMassFlux_(dimensionedScalar("zero", dimMass/dimTime, 0.0)),
    totalHeatRR_(dimensionedScalar("zero", dimEnergy/dimTime, 0.0)),
    timeChem_(1.0)
{

    mesh.setFluxRequired(T_.name());

    ST_ = STmodel_->ST()();
    CONV_ = CONV();
    rho0_.ref() = rho_.ref();

    subintegrateSwitch_ = coeffs().lookupOrDefault("subintegrateHeatTransfer",false);
    bedMotionSwitch_ = coeffs().lookupOrDefault("bedCollapse",false);
    advectSolidFields_ = coeffs().lookupOrDefault("advectSolidFields", true);
    replenishSwitch_ = coeffs().lookupOrDefault("replenish",false);
    critPorosity_ = coeffs().lookupOrDefault("criticalPorosity",0.9999);

    Info << endl;
    Info << "subintegrateHeatTransfer " << subintegrateSwitch_ << endl;
    Info << "bedCollapse              " << bedMotionSwitch_    << endl;
    Info << "advectSolidFields        " << advectSolidFields_  << endl;
    if (bedMotionSwitch_) 
    {
        Info << "criticalPorosity         " << critPorosity_  << endl;
    }
    Info << "replenish                " << replenishSwitch_    << endl;
    Info << endl;

    forAll(Ys_, fieldI)
    {
          Ym_.set
          (
                fieldI,
                new volScalarField  //DasteXar
                    (
                        IOobject
                        (
                            Ys_[fieldI].name() + "m",
                            time_.timeName(),
                            mesh_,
                            IOobject::READ_IF_PRESENT,
                            IOobject::AUTO_WRITE
                        ),
                        mesh_,
                        dimensionedScalar("zero", dimMass/dimVolume, 0.0),
                        zeroGradientFvPatchScalarField::typeName
                    )
          );
          Ym_[fieldI] = Ys_[fieldI] * rho_ * (1.0 - porosity_); //DasteXar
    }

    forAll(rho_,cellI)
    {
        //if (porosity_[cellI] < 1.)
        if (porosity_[cellI] < critPorosity_) //DasteXar
        {
             whereIsNot_[cellI] = 0.;
             whereIs_[cellI] = 1.;
        }
        else
        {
             whereIsNot_[cellI] = 1.;
             whereIs_[cellI] = 0.;
             rho0_[cellI] = 3.14;
        }
    }

    if (active_)
    {
        read();
    }

    forAll(whereIs_, cellI)
    {
        if (whereIs_[cellI] == 1)
        {
            bool surfC = false;
            forAll(mesh_.cellCells()[cellI],cellJ)
            {
                if (whereIs_[mesh_.cellCells()[cellI][cellJ]] == 0)
                {
                    surfC = true;
                }
            }
            if (surfC) surfF_[cellI] = 1;
        }
    }

    forAll(cellVolume_,cellI)
    {
        cellVolume_[cellI] = mesh_.V()[cellI];
    }
    cellVolume_.correctBoundaryConditions();

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
    return surfF_;
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

    // Iterates over every cell and sets cells containing solid phase
    // as reacting cell.
    forAll(T_, cellI)
    {
        scalar solidMassFractionSum = 0.0;

        for (label i = 0; i < Ys_.size(); ++i)
        {
            solidMassFractionSum += max(Ys_[i][cellI], scalar(0.0));
        }

        if
        (
            active_
        //  && porosity_[cellI] < 1.0 - SMALL
        && porosity_[cellI] < critPorosity_ //DasteXar
         && porosity_[cellI] >= 0.0
         && solidMassFractionSum > SMALL
        )
        {
            solidChemistry_->setCellReacting(cellI, true);
        }
        else
        {
            solidChemistry_->setCellReacting(cellI, false);
        }
    }

}


void volPyrolysis::transferSolidStateFromDEM
(
    const DynamicList<label>& fromCells,
    const DynamicList<label>& toCells,
    const volScalarField& nParticles
)
{
    //if (!active_) return;

    forAll(fromCells, moveI)
    {
        const label fromCell = fromCells[moveI];
        const label toCell = toCells[moveI];

        if (fromCell < 0 || toCell < 0) continue;
        if (fromCell >= mesh_.nCells() || toCell >= mesh_.nCells()) continue;
        if (fromCell == toCell) continue;

        // Only transfer from cells that actually contain solid state.
        if (porosity_[fromCell] >= 1.0 - SMALL) continue;

        porosity_[toCell] = porosity_[fromCell];
        porosityArch_[toCell] = porosityArch_[fromCell];
        T_[toCell] = T_[fromCell];
        rho_[toCell] = rho_[fromCell];

        for (label i = 0; i < Ys_.size(); ++i)
        {
            Ys_[i][toCell] = Ys_[i][fromCell];
            Ym_[i][toCell] = Ym_[i][fromCell];
        }


        porosity_.oldTime()[toCell] = porosity_[toCell];
        porosityArch_.oldTime()[toCell] = porosityArch_[toCell];
        T_.oldTime()[toCell] = T_[toCell];
        rho_.oldTime()[toCell] = rho_[toCell];

        for (label i = 0; i < Ys_.size(); ++i)
        {
            Ys_[i].oldTime()[toCell] = Ys_[i][toCell];
            Ym_[i].oldTime()[toCell] = Ym_[i][toCell];
        }



        // If no DEM particle remains in the old cell, make it gas.
        if (nParticles[fromCell] < 0.5)
        {
            porosity_[fromCell] = 1.0;
            porosityArch_[fromCell] = 1.0;

            for (label i = 0; i < Ys_.size(); ++i)
            {
                Ym_[i][fromCell] = 0.0;
                Ys_[i][fromCell] = 0.0;
            }


            porosity_.oldTime()[fromCell] = porosity_[fromCell];
            porosityArch_.oldTime()[fromCell] = porosityArch_[fromCell];
            T_.oldTime()[fromCell] = T_[fromCell];
            rho_.oldTime()[fromCell] = rho_[fromCell];

            for (label i = 0; i < Ys_.size(); ++i)
            {
                Ys_[i].oldTime()[fromCell] = Ys_[i][fromCell];
                Ym_[i].oldTime()[fromCell] = Ym_[i][fromCell];
            }
        }
    }

    porosity_.correctBoundaryConditions();
    porosityArch_.correctBoundaryConditions();
    T_.correctBoundaryConditions();
    rho_.correctBoundaryConditions();

    for (label i = 0; i < Ys_.size(); ++i)
    {
        Ys_[i].correctBoundaryConditions();
        Ym_[i].correctBoundaryConditions();
    }



        forAll(porosity_, cellI)
    {
        porosity_[cellI] = min(max(porosity_[cellI], scalar(0.0)), scalar(1.0));
        porosityArch_[cellI] = min(max(porosityArch_[cellI], scalar(0.0)), scalar(1.0));
    }





    forAll(porosity_, cellI)
    {
        //if (porosity_[cellI] < 1.0)
        if (porosity_[cellI] < critPorosity_) //DasteXar
        {
            whereIs_[cellI] = 1.0;
            whereIsNot_[cellI] = 0.0;
        }
        else
        {
            whereIs_[cellI] = 0.0;
            whereIsNot_[cellI] = 1.0;
        }
    }

    whereIs_.correctBoundaryConditions();
    whereIsNot_.correctBoundaryConditions();
}



void volPyrolysis::evolveRegion()
{
    voidFraction_ = porosity_;

    if (equilibrium_)
    {}
    else
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

    //DasteXar changed the order of solving otherwise only porosity moves not Y_
    evolvePorosity();
    solveSpeciesMass();


    solidThermo_.correct(); // eqZx2uHGn046

    if (equilibrium_)
    {}
    else
    {
        for (int nonOrth = 0; nonOrth <= nNonOrthCorr_; ++nonOrth)
        {
            solveEnergy();
        }
    }

    calculateMassTransfer();
    info();
}

void volPyrolysis::evolvePorosity()
{
    if (active_)
    {
        porositySource_ = solidChemistry_->RRpor(T_)();

        volScalarField& por = porosity_;

        surfaceScalarField Us
        (
            IOobject
            (
                "solidPorosityFlux",
                time_.timeName(),
                mesh_,
                IOobject::NO_READ,
                IOobject::NO_WRITE
            ),
            mesh_,
            dimensionedScalar("zero", dimVolume/dimTime, 0.0)
        );

        if (advectSolidFields_)
        {
            Us = mesh_.Sf() & fvc::interpolate(Us_,"Us");
        }

        // requires setting same stuff as for diffusion to release flux at the ends of porous media
        // it would be best to solve 1-porosity as it gives 0 flux naturally when empty?? 
        // fvScalarMatrix porosityEqn
        // (
        //     fvm::ddt(por)
        //  ==
        //     porositySource_
        //   - fvc::div(Us,por,"div(phiSolid)")
        // );

        //DasteXar
        fvScalarMatrix porosityEqn
            (
                fvm::ddt(por)
            ==
                porositySource_
            + fvc::div(Us)
            - fvc::div(Us, por, "div(phiSolid)")
            );




        porosityEqn.solve("porosity");

        forAll(porosity_, cellI)
        {
            // DasteXar changed scalar(0.0) to scalar(1e-4) to prevent floating error 
            porosity_[cellI] = min(max(porosity_[cellI], scalar(1e-4)), scalar(1.0));
        }

        Info<< "porosity equation solved. Sources min/max   = " << gMin(porositySource_)
            << ", " << gMax(porositySource_);

        Info<< "; values min Y = " << gMin(por)
            <<" max Y = " << gMax(por) << endl;

        //scalar minTs = min(max(gAverage(T_), scalar(200.0)), scalar(6000.0));
        scalar minTs = -1.;



        FIFOStack<label> flippedStack = {};

        volVectorField whereIsGrad = fvc::grad(whereIs_);        

        forAll(porosity_,cellI)
        {
            
            // for bed-motion
            // a cell is added to the collapse/motion list after crossing the threshold
            // . The upper limit excludes cells that are already fully
            // converted to gas.
            if
            (
                porosity_[cellI] > critPorosity_
            && porosity_[cellI] < 1.0 - SMALL
            && (Us_[cellI] & whereIsGrad[cellI]) > 0
            )
            {
                flippedStack.push(cellI);
            }



            //if (porosity_[cellI] < 1.0)
            if (porosity_[cellI] < critPorosity_)  //DasteXar

            {
                whereIs_[cellI] = 1.0;
                whereIsNot_[cellI] = 0.0;
            }
            else
            {
                whereIs_[cellI] = 0.0;
                whereIsNot_[cellI] = 1.0;
            }
        }

        List<Field<label>> procFlipList(Pstream::nProcs());
        procFlipList[Pstream::myProcNo()] = labelList(flippedStack);
        Pstream::gatherList(procFlipList);
        Pstream::scatterList(procFlipList);
        bool evaluate = false;
        forAll(procFlipList,listI)
        {
            if (procFlipList[listI].size() > 0)
            {
                evaluate = true;
            }
        }

        if (bedMotionSwitch_)
        {
            if (evaluate)
            {
                whereIs_.correctBoundaryConditions();
                porosity_.correctBoundaryConditions();
                whereWas_ = whereWas_*0;

                // this part collects global cell numbering
                List<Field<scalar>> globalIndex(Pstream::nProcs());
                globalIndex[Pstream::myProcNo()] = whereWas_.internalField();
                Pstream::gatherList(globalIndex);
                if (Pstream::master())
                {
                    labelList sizes
                    (
                        ListListOps::subSizes(globalIndex,accessOp<Field<scalar>>())
                    );

                    label prefix = 0;
                    forAll(globalIndex,gI)
                    {
                        forAll(globalIndex[gI],entI)
                        {
                            globalIndex[gI][entI] = entI + prefix;
                        }
                        prefix = prefix + sizes[gI];
                    }
                }
                Pstream::scatter(globalIndex);
                volScalarField globalIndices = whereIs_*0;
                forAll(globalIndices,cellI)
                {
                    globalIndices[cellI] = globalIndex[Pstream::myProcNo()][cellI];
                }
                globalIndices.correctBoundaryConditions();

                // this part determines processor based a possible motion paths using the global numbering
                // it writes into takeFrom the global adress of cell from which the resources will be taken
                volScalarField takeFrom = whereIs_;
                forAll(mesh_.cells(),cellI)
                {
                    if (whereIs_[cellI] < 1)
                    {
                        takeFrom[cellI] = -1;
                    }
                    else
                    { 
                        label takeFromGlobalID = -1;
                        forAll(mesh_.cells()[cellI],faceI)
                        {
                            if (mesh_.Cf()[mesh_.cells()[cellI][faceI]].z() > mesh_.C()[cellI].z() + 1e-6) // determine the upper face - here more advanced criterion should be in force
                            {                                                                              // like eg. upwards gravity maybe with some uniqueness cirteria
                                label faceID = -1;
                                label patchID = mesh_.boundaryMesh().whichPatch(mesh_.cells()[cellI][faceI]);
                                if (patchID > -1)
                                {
                                    faceID = mesh_.boundaryMesh()[patchID].whichFace(mesh_.cells()[cellI][faceI]);
                                    if (isA<processorPolyPatch>(mesh_.boundaryMesh()[patchID]))
                                    {
                                        takeFromGlobalID = globalIndices.boundaryField()[patchID].patchNeighbourField()()[faceID];
                                        if (whereIs_.boundaryField()[patchID].patchNeighbourField()()[faceID] < 1)
                                        {
                                            takeFromGlobalID = -1;
                                        }
                                    }
                                }
                                else
                                {
                                    
                                    if (cellI == mesh_.faceNeighbour()[mesh_.cells()[cellI][faceI]])
                                    {
                                        takeFromGlobalID =  mesh_.faceOwner()[mesh_.cells()[cellI][faceI]]; 
                                    }
                                    else
                                    {
                                        takeFromGlobalID =  mesh_.faceNeighbour()[mesh_.cells()[cellI][faceI]]; 
                                    }
                                    if (whereIs_[takeFromGlobalID] < 1)
                                    {
                                        takeFromGlobalID = -1;
                                    }
                                    else
                                    {
                                        takeFromGlobalID = globalIndices[takeFromGlobalID];
                                    }
                                } 
                                //Pout << cellI << " "  << Pstream::myProcNo()  << " " << globalIndex[Pstream::myProcNo()][cellI] 
                                //     << " "  << mesh_.cellCells()[cellI].size()  << " "
                                //     << mesh_.Cf()[mesh_.cells()[cellI][faceI]] << " " << mesh_.Sf()[mesh_.cells()[cellI][faceI]] << " " 
                                //     << patchID << " " << faceID << " " << mesh_.faceNeighbour()[mesh_.cells()[cellI][faceI]] << " " << mesh_.faceOwner()[mesh_.cells()[cellI][faceI]] 
                                //     << " " << takeFromGlobalID << endl;
                            }
                        }
                        takeFrom[cellI] = takeFromGlobalID; 
                    }
                }

                //whereWas_ = takeFrom;

                // this part gets motion paths to be distributed per each processor
                // from possible routes and initial positions
                // and maximum reaches of motion paths
                // convenient way might be to get back the initial cells or whole routes on each processor
                List<Field<scalar>> routes(Pstream::nProcs());
                routes[Pstream::myProcNo()] = takeFrom.internalField();
                Pstream::gatherList(routes);

                // this part calcuates global actual routes of material travel
                // and distributes them to execution on local processors
                List<List<label>> realRoutes = {};
                scalar replenishedMass = 0;
                if (Pstream::master())
                {

                    Field<scalar> routesCombined =
                    ListListOps::combine<Field<scalar>>
                    (
                        routes,
                        accessOp<Field<scalar>>()
                    );

                    forAll(procFlipList,lI)
                    {
                        forAll(procFlipList[lI],entI)
                        {
                            //Info << procFlipList[lI][entI] << " " << globalIndex[lI][procFlipList[lI][entI]] << " start ";
                            label prevStep = globalIndex[lI][procFlipList[lI][entI]];
                            List<label> realRoute = {prevStep};
                            while (routesCombined[prevStep] >= 0)
                            {
                                //Info << " " << prevStep;
                                prevStep = routesCombined[prevStep];
                                realRoute.append(prevStep);
                            }
                            //Info << endl;
                            realRoutes.append(realRoute);
                        }
                    }
                    //Info << realRoutes << endl;
                }
                Pstream::scatter(realRoutes);

                //this part will determine processorwise route parts and motions of porous media
                whereWas_ = whereWas_*0;
                porosity_.correctBoundaryConditions();
                porosityArch_.correctBoundaryConditions();
                T_.correctBoundaryConditions();
                rho_.correctBoundaryConditions();
                for (label i = 0; i < Ys_.size(); ++i)
                {
                    Ym_[i].correctBoundaryConditions();
                    Ys_[i].correctBoundaryConditions();
                }
                forAll(realRoutes,routeI)
                {
                    label minLocalGlobalI = globalIndex[Pstream::myProcNo()][0];
                    label maxLocalGlobalI = globalIndex[Pstream::myProcNo()][globalIndex[Pstream::myProcNo()].size()-1];
                    bool currentI = false;
                    bool previousI = false;
                    for (label stepI = 0; stepI < realRoutes[routeI].size(); stepI++)
                    {
                        if ( (minLocalGlobalI <= realRoutes[routeI][stepI]) and (realRoutes[routeI][stepI] <= maxLocalGlobalI))
                        {
                            //Pout << realRoutes[routeI][stepI] << " " << realRoutes[routeI][stepI] - minLocalGlobalI << " ";
                            previousI = currentI;
                            currentI = true;
                            whereWas_[realRoutes[routeI][stepI] - minLocalGlobalI] = -1;
                        }
                        else
                        {
                            previousI = currentI;
                            currentI = false;
                        }
                        if (previousI and currentI)
                        {
                            //Pout << "motion inside single core from " << realRoutes[routeI][stepI] << " " << realRoutes[routeI][stepI] - minLocalGlobalI 
                            //     << " to " << realRoutes[routeI][stepI-1] << " " << realRoutes[routeI][stepI-1] - minLocalGlobalI
                            //     << " porosity from " << porosity_[realRoutes[routeI][stepI] - minLocalGlobalI]
                            //     << " porosity to " << porosity_[realRoutes[routeI][stepI-1] - minLocalGlobalI]
                            //     << endl;
                            scalar cellVolumeRatio = cellVolume_[realRoutes[routeI][stepI] - minLocalGlobalI]/cellVolume_[realRoutes[routeI][stepI-1] - minLocalGlobalI];
                            // Pout << " " <<  1. - (1. - porosity_[realRoutes[routeI][stepI] - minLocalGlobalI])*cellVolumeRatio << endl;
                            // this 0.001 is a guardian for too large skewness in cells. It introduces error and will be solved in the future.
                            porosity_[realRoutes[routeI][stepI-1] - minLocalGlobalI] = max(1. - (1. - porosity_[realRoutes[routeI][stepI] - minLocalGlobalI])*cellVolumeRatio
                                                                                          ,0.001);
                            porosityArch_[realRoutes[routeI][stepI-1] - minLocalGlobalI] = max(1. - (1. - porosityArch_[realRoutes[routeI][stepI] - minLocalGlobalI])*cellVolumeRatio
                                                                                          ,0.001);
                            T_[realRoutes[routeI][stepI-1] - minLocalGlobalI] = T_[realRoutes[routeI][stepI] - minLocalGlobalI];
                            rho_[realRoutes[routeI][stepI-1] - minLocalGlobalI] = rho_[realRoutes[routeI][stepI] - minLocalGlobalI];
                            for (label i = 0; i < Ys_.size(); ++i)
                            {
                                Ym_[i][realRoutes[routeI][stepI-1] - minLocalGlobalI] = Ym_[i][realRoutes[routeI][stepI] - minLocalGlobalI];
                                Ys_[i][realRoutes[routeI][stepI-1] - minLocalGlobalI] = Ys_[i][realRoutes[routeI][stepI] - minLocalGlobalI];
                            }
                            
                            //scalar neededMass = porosity_[realRoutes[routeI][stepI-1] - minLocalGlobalI]*(1. - porosity_[realRoutes[routeI][stepI] - minLocalGlobalI])
                            //                    *cellVolume_[realRoutes[routeI][stepI-1] - minLocalGlobalI]*rho_[realRoutes[routeI][stepI] - minLocalGlobalI];
                            //scalar availableMass = (1. - porosity_[realRoutes[routeI][stepI] - minLocalGlobalI])
                            //                    *cellVolume_[realRoutes[routeI][stepI] - minLocalGlobalI]*rho_[realRoutes[routeI][stepI] - minLocalGlobalI];
                            //scalar movingMass = min(neededMass,availableMass);
                            //Pout << " needed mass " << neededMass 
                            //     << " available mass " << availableMass
                            //     << " moving mass " << movingMass 
                            //     << endl;
                            //porosity_[realRoutes[routeI][stepI-1] - minLocalGlobalI] -= movingMass/rho_[realRoutes[routeI][stepI] - minLocalGlobalI];
                            //porosity_[realRoutes[routeI][stepI] - minLocalGlobalI] += movingMass/rho_[realRoutes[routeI][stepI] - minLocalGlobalI]; 
                            //scalar totalMass   = 0;
                            //scalar totalMassUp = 0;
                            //for (label i = 0; i < Ys_.size(); ++i)
                            //{
                            //    Ym_[i][realRoutes[routeI][stepI-1] - minLocalGlobalI] += movingMass*Ys_[i][realRoutes[routeI][stepI] - minLocalGlobalI];
                            //    Ym_[i][realRoutes[routeI][stepI] - minLocalGlobalI]   -= movingMass*Ys_[i][realRoutes[routeI][stepI] - minLocalGlobalI];
                            //    totalMass   += Ym_[i][realRoutes[routeI][stepI-1] - minLocalGlobalI];
                            //    totalMassUp +=Ym_[i][realRoutes[routeI][stepI] - minLocalGlobalI];
                            //}
                            //for (label i = 0; i < Ys_.size(); ++i)
                            //{
                            //    Ys_[i][realRoutes[routeI][stepI-1] - minLocalGlobalI] = Ym_[i][realRoutes[routeI][stepI-1] - minLocalGlobalI] / totalMass;
                            //    Ys_[i][realRoutes[routeI][stepI] - minLocalGlobalI]   = Ym_[i][realRoutes[routeI][stepI] - minLocalGlobalI] / totalMassUp;
                            //}

                            //scalar cellVolumeRatio = cellVolume_[realRoutes[routeI][stepI] - minLocalGlobalI]/cellVolume_[realRoutes[routeI][stepI-1] - minLocalGlobalI];
                            //Pout << " " <<  1. - (1. - porosity_[realRoutes[routeI][stepI] - minLocalGlobalI])*cellVolumeRatio << endl;
                            //if (1. - (1. - porosity_[realRoutes[routeI][stepI] - minLocalGlobalI])*cellVolumeRatio <= 0.001)
                            //{
                            //    scalar neededMass = (porosity_[realRoutes[routeI][stepI-1] - minLocalGlobalI] - 0.001)
                            //                       *cellVolume_[realRoutes[routeI][stepI-1] - minLocalGlobalI]*rho_[realRoutes[routeI][stepI] - minLocalGlobalI];
                            //    porosity_[realRoutes[routeI][stepI-1] - minLocalGlobalI] = 0.001;
                            //    porosity_[realRoutes[routeI][stepI] - minLocalGlobalI] += neededMass/rho_[realRoutes[routeI][stepI] - minLocalGlobalI];

                            //    scalar totalMassDown = 0;
                            //    scalar totalMassUp   = 0;
                            //    for (label i = 0; i < Ys_.size(); ++i)
                            //    {
                            //        Ym_[i][realRoutes[routeI][stepI-1] - minLocalGlobalI] += neededMass*Ys_[i][realRoutes[routeI][stepI] - minLocalGlobalI];
                            //        Ym_[i][realRoutes[routeI][stepI] - minLocalGlobalI]   -= neededMass*Ys_[i][realRoutes[routeI][stepI] - minLocalGlobalI];
                            //        totalMassDown += Ym_[i][realRoutes[routeI][stepI-1] - minLocalGlobalI];
                            //        totalMassUp   += Ym_[i][realRoutes[routeI][stepI] - minLocalGlobalI];
                            //    }
                            //    for (label i = 0; i < Ys_.size(); ++i)
                            //    {
                            //        Ys_[i][realRoutes[routeI][stepI-1] - minLocalGlobalI] = Ym_[i][realRoutes[routeI][stepI-1] - minLocalGlobalI] / totalMassDown;
                            //        Ys_[i][realRoutes[routeI][stepI] - minLocalGlobalI]   = Ym_[i][realRoutes[routeI][stepI] - minLocalGlobalI]   / totalMassUp;
                            //    }
                            //}
                            //else
                            //{
                            //    porosity_[realRoutes[routeI][stepI-1] - minLocalGlobalI] = 1. - (1. - porosity_[realRoutes[routeI][stepI] - minLocalGlobalI])*cellVolumeRatio;
                            //    porosityArch_[realRoutes[routeI][stepI-1] - minLocalGlobalI] = 1. - (1. - porosityArch_[realRoutes[routeI][stepI] - minLocalGlobalI])*cellVolumeRatio;
                            //    T_[realRoutes[routeI][stepI-1] - minLocalGlobalI] = T_[realRoutes[routeI][stepI] - minLocalGlobalI];
                            //    rho_[realRoutes[routeI][stepI-1] - minLocalGlobalI] = rho_[realRoutes[routeI][stepI] - minLocalGlobalI];
                            //    for (label i = 0; i < Ys_.size(); ++i)
                            //    {
                            //        Ym_[i][realRoutes[routeI][stepI-1] - minLocalGlobalI] = Ym_[i][realRoutes[routeI][stepI] - minLocalGlobalI];
                            //        Ys_[i][realRoutes[routeI][stepI-1] - minLocalGlobalI] = Ys_[i][realRoutes[routeI][stepI] - minLocalGlobalI];
                            //    }
                            //}

                        }
                        if (previousI and (not currentI))
                        {
                            //Pout << "motion between cores from " << realRoutes[routeI][stepI] << " on other core to " 
                            //     << realRoutes[routeI][stepI-1] << " " << realRoutes[routeI][stepI-1] - minLocalGlobalI << endl;
                            forAll(mesh_.cells()[realRoutes[routeI][stepI-1] - minLocalGlobalI],faceI)
                            {
                                 label faceID = -1;
                                 label patchID = mesh_.boundaryMesh().whichPatch(mesh_.cells()[realRoutes[routeI][stepI-1] - minLocalGlobalI][faceI]);
                                 if (patchID > -1)
                                 {
                                     faceID = mesh_.boundaryMesh()[patchID].whichFace(mesh_.cells()[realRoutes[routeI][stepI-1] - minLocalGlobalI][faceI]);
                                     if (isA<processorPolyPatch>(mesh_.boundaryMesh()[patchID]))
                                        {
                                            //DasteXar to pass values from one subdomain to another
                                            label neighbourGlobalID =
                                                globalIndices.boundaryField()[patchID].patchNeighbourField()()[faceID];

                                            if (neighbourGlobalID != realRoutes[routeI][stepI])
                                            {
                                                continue;
                                            }

                                            scalar cellVolumeRatio =
                                                cellVolume_.boundaryField()[patchID].patchNeighbourField()()[faceID]
                                            / cellVolume_[realRoutes[routeI][stepI-1] - minLocalGlobalI];

                                            porosity_[realRoutes[routeI][stepI-1] - minLocalGlobalI] =
                                                max
                                                (
                                                    1. - (1. - porosity_.boundaryField()[patchID].patchNeighbourField()()[faceID])
                                                    * cellVolumeRatio,
                                                    0.001
                                                );
                                         porosityArch_[realRoutes[routeI][stepI-1] - minLocalGlobalI] = max(1. - (1. - porosityArch_.boundaryField()[patchID].patchNeighbourField()()[faceID])*cellVolumeRatio
                                                                                                        ,0.001);
                                         T_[realRoutes[routeI][stepI-1] - minLocalGlobalI] = T_.boundaryField()[patchID].patchNeighbourField()()[faceID];
                                         rho_[realRoutes[routeI][stepI-1] - minLocalGlobalI] = rho_.boundaryField()[patchID].patchNeighbourField()()[faceID];
                                         for (label i = 0; i < Ys_.size(); ++i)
                                         {
                                             Ym_[i][realRoutes[routeI][stepI-1] - minLocalGlobalI] = Ym_[i].boundaryField()[patchID].patchNeighbourField()()[faceID];
                                             Ys_[i][realRoutes[routeI][stepI-1] - minLocalGlobalI] = Ys_[i].boundaryField()[patchID].patchNeighbourField()()[faceID];
                                         }
                                     }
                                 }
                            }
                        } 
                    }
                    if ( (minLocalGlobalI <= realRoutes[routeI][realRoutes[routeI].size() - 1]) and (realRoutes[routeI][realRoutes[routeI].size() - 1] <= maxLocalGlobalI))
                    {
                        //Pout << "replenish last one " << realRoutes[routeI][realRoutes[routeI].size() - 1]  << " " << realRoutes[routeI][realRoutes[routeI].size() - 1] - minLocalGlobalI << endl;
                        //whereWas_[realRoutes[routeI][realRoutes[routeI].size() - 1] - minLocalGlobalI] = -1;
                        //this line is to add cold feedstock for replenishing
                        //however it has been commented out as it has to be specified
                        //T_[realRoutes[routeI][realRoutes[routeI].size() - 1] - minLocalGlobalI] = 300;
                        //this lines are to stop replenishing
                        if (not replenishSwitch_)
                        {
                            porosity_[realRoutes[routeI][realRoutes[routeI].size() - 1] - minLocalGlobalI] = 1.;
                            porosityArch_[realRoutes[routeI][realRoutes[routeI].size() - 1] - minLocalGlobalI] = 1.;
                        }
                        replenishedMass += (1. - porosity_[realRoutes[routeI][realRoutes[routeI].size() - 1] - minLocalGlobalI])*
                            rho_[realRoutes[routeI][realRoutes[routeI].size() - 1] - minLocalGlobalI]*
                            cellVolume_[realRoutes[routeI][realRoutes[routeI].size() - 1] - minLocalGlobalI];
                        //Pout << " mass replenished on core " << Pstream::myProcNo()  << " is " 
                        //     <<  replenishedMass << " from cellI " << realRoutes[routeI][realRoutes[routeI].size() - 1] - minLocalGlobalI 
                        //     << " " << rho_[realRoutes[routeI][realRoutes[routeI].size() - 1] - minLocalGlobalI] 
                        //     << " " << cellVolume_[realRoutes[routeI][realRoutes[routeI].size() - 1] - minLocalGlobalI] 
                        //     << " " << porosity_[realRoutes[routeI][realRoutes[routeI].size() - 1] - minLocalGlobalI] << endl;
                    }
                }

                reduce(replenishedMass, sumOp<scalar>());
                if (Pstream::master())
                {
                    totRepMass_ += replenishedMass;
                    Info << "Total replenished mass in this run is: " << totRepMass_ << " [kg]" <<endl;
                }
            }            
        }
        else
        {
            // this is to set porosity 0 and other fields on flipped fileds
            // it will be an alternative to the motion procedure at some point
            List<label> flippedStackList = labelList(flippedStack);
            forAll(flippedStackList,entI)
            {
                porosity_[flippedStackList[entI]] = 1.0;
                T_[flippedStackList[entI]] = minTs;
            }
        }

        forAll(porosity_,cellI)
        {
            // if (porosity_[cellI] < 0.0001)
            // {
            //     porosity_[cellI] = 0.0;
            //     Info << "b porosity 0 in cell " << cellI << endl;
            // }


            //if (porosity_[cellI] < 1.0)
            if (porosity_[cellI] < critPorosity_) //DasteXar
            {
                whereIs_[cellI] = 1.0;
                whereIsNot_[cellI] = 0.0;
            }
            else
            {
                whereIs_[cellI] = 0.0;
                whereIsNot_[cellI] = 1.0;
            }
        }

        surfF_= surfF_*0;
        porosity_.correctBoundaryConditions();
        whereIs_.correctBoundaryConditions();
        surfaceScalarField  whereIsPatch  = fvc::interpolate(whereIs_);
        forAll(whereIsPatch,faceI)
        {
            if ( (whereIsPatch[faceI] > 0) and (whereIsPatch[faceI] < 1) )
            {
                if (whereIs_[mesh_.owner()[faceI]] == 1)
                {
                    surfF_[mesh_.owner()[faceI]] = 1;
                }
                if (whereIs_[mesh_.neighbour()[faceI]] == 1)
                {
                    surfF_[mesh_.neighbour()[faceI]] = 1;
                }
            }
        }
        forAll(whereIsPatch.boundaryField(),patchI)
        {
            if (isA<processorPolyPatch>(mesh_.boundaryMesh()[patchI]))
            {
                forAll(whereIsPatch.boundaryField()[patchI],faceI)
                {
                    if ( (whereIsPatch.boundaryField()[patchI][faceI] > 0) and (whereIsPatch.boundaryField()[patchI][faceI] < 1) )
                    {
                        if (whereIs_[mesh_.owner()[faceI + mesh_.boundaryMesh()[patchI].start()]] == 1)
                        {
                            surfF_[mesh_.owner()[faceI + mesh_.boundaryMesh()[patchI].start()]] = 1;
                        }
                    }
                }
            }
        }
    }
    else
    {}
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
            dimensionedScalar
            (
                "zero",
                dimMass/dimVolume/dimTime,
                0.0
            )
        )
    );

    if (active_)
    {
        volScalarField& totalGasSource = tSrho.ref();

        const speciesTable& gasTable =
            solidChemistry_->gasTable();

        // RRg is calculated by the chemistry model per total computational-cell volume [kg/(m3_cell s)].
        forAll(gasTable, gasI)
        {
            totalGasSource += solidChemistry_->RRg(gasI)();
        }

        const volScalarField alphaS
        (
            max
            (
                scalar(1.0) - porosity_,
                dimensionedScalar
                (
                    "minAlphaS",
                    dimless,
                    SMALL
                )
            )
        );

        // The existing porousGasificationFoam solver multiplies Srho()
        // by (1-porosityF) in rhoEqn, pEqn and pcEqn.
        //
        // Therefore this total source must be returned per unit solid volume
        // . The multiplication in the solver converts it back to
        // the original bulk-cell RRg source:
        //
        //     alphaS * (RRg/alphaS) = RRg
        //
        // Srho(i) must NOT be changed because YEqn inserts individual
        // gas-species sources directly without multiplying by alphaS.

        //The chemistry model calculates RRg but solver uses uses total Srho() as (Srho * (1.0 - porosityF)) in
        //rhoEqn.H , qn.H and  pcEqn.H while while gas species use pyrolysisZone.Srho(i)
        //Without this correction total gas mass added to continuity ≠ sum of gas species mass added

        totalGasSource =
            whereIs_ * totalGasSource / alphaS;

        totalGasSource.correctBoundaryConditions();
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

Foam::tmp<Foam::volScalarField> volPyrolysis::heatTransfer()
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

    if(equilibrium_)
    {}
    else
    {
        if (subintegrateSwitch_)
        {
            volScalarField rhoCpG(gasThermo_.rho() * gasThermo_.Cp() * porosity_);
            volScalarField Tgas = gasThermo_.T();
            // volScalarField rhoCpS
            //     (
            //             max
            //             (
            //                 rho_ * solidThermo_.Cp() * (1 - porosity_),
            //                 dimensionedScalar("minRhoCp",dimEnergy/dimTemperature/dimVolume,SMALL)
            //             )
            //     );


            // Use the same conserved solid mass concentration as solveEnergy().
            // This keeps the  gas-solid heat-transfer subintegration consistent after chemistry and solid advection.
            volScalarField totalYm(0.0 * Ym_[0]);

            for (label i = 0; i < Ym_.size(); ++i)
            {
                totalYm += Ym_[i];
            }

            volScalarField rhoCpS
            (
                max
                (
                    totalYm * solidThermo_.Cp(),
                    dimensionedScalar
                    (
                        "minRhoCp",
                        dimEnergy/dimTemperature/dimVolume,
                        SMALL
                    )
                )
            );



            volScalarField deltaTemp(T_ * 0);
            scalar deltaTime = time_.deltaTValue();

            forAll(deltaTemp,cellI)
            {
                //if (whereIs_[cellI] == 1.0 && CONV_[cellI] > 0.0)
                //DasteXar replacing the logic of clipping porosityF smaller than 0.0001
                if  
                    (
                        whereIs_[cellI] == 1.0
                    && CONV_[cellI] > 0.0
                    && rhoCpG[cellI] > VSMALL
                    )
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
                Sh_.ref()[cellI] = deltaTemp[cellI] * rhoCpG[cellI] * whereIs_[cellI] / deltaTime;
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
            Sh_ = HT * whereIs_;
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

    if(equilibrium_)
    {}
    else
    {
        CONVloc_ = HTmodel_->CONV()*whereIs_;
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

        if (equilibrium_)
        {}
        else // eqZx2uHGn018
        {
            volScalarField tempSh = hSh_();
            tempSh = gasThermo_.Cp() * (T_ - gasThermo_.T()) * Srho();
            hSh_ = tempSh * whereIs_ * (1 - porosity_);
        }
    }

    return hSh_;
}

Foam::tmp<Foam::volScalarField> volPyrolysis::heatUpGas() const
{
    return heatUpGas_;
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
        << indent << "Total mass replenished   [kg] = " << totRepMass_ << nl
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
