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
    // Reconstruct the mass fractions Ys_ (consumed by chemistry and thermo)
    // from the transported mass concentrations Ym_ [kg/m3]. The condition is
    // the solid inventory of the cell, not whereIs_: that mask is refreshed
    // in recoverPorosity(), one stage later in the time step, so it still
    // reads empty in a cell that solid mass has just advected into. Ys_ left
    // stale there leaves rho_ stale too, and rho_ is only a valid mixture
    // density where sum_i Ys_i = 1 - which is what recoverPorosity() divides
    // the solid mass by.
    forAll(whereIs_, cellI)
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
        else if (whereIs_[cellI] == 1)
        {
            // A cell that held solid at the last porosity update and has
            // none left: assign a default composition so rho_ and Cp stay
            // defined. A cell that has never held solid keeps the
            // composition it was initialised with, which serves the same
            // purpose.
            forAll(Ys_, i)
            {
                Ys_[i][cellI] = 0.0;
            }
            Ys_[0][cellI] = 1.0;
        }
    }
}

tmp<surfaceScalarField> volPyrolysis::solidVolFlux() const
{
    tmp<surfaceScalarField> tSolidVolFlux
    (
        new surfaceScalarField
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
        )
    );

    if (advectSolidFields_)
    {
        tSolidVolFlux.ref() = mesh_.Sf() & fvc::interpolate(Us_, "Us");
    }

    return tSolidVolFlux;
}

label volPyrolysis::firstInvalidCell
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

void volPyrolysis::reportInvalidSolidState
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

void volPyrolysis::recoverPorosity()
{
    if (active_)
    {
        // Chemistry contribution to d(1-por)/dt. Reported below, and not
        // applied: RRpor = -sum_i RRs_i/rho_i is exactly the time derivative
        // of sum_i Ym_i/rho_i, and chemistry has already moved that sum
        // through RRs_i in solveSpeciesMass(). Adding it to the porosity as
        // well would count it twice.
        porositySource_ = solidChemistry_->RRpor(T_)();

        volScalarField& por = porosity_;

        surfaceScalarField phiUs(solidVolFlux());

        volScalarField totalYm = 0*Ym_[0];

        forAll(Ym_, i)
        {
            totalYm += Ym_[i];
        }

        // The porosity is not transported. It is the void the solid mass of
        // the cell leaves behind,
        //
        //     1 - por = sum_i Ym_i/rho_i = totalYm/rho_
        //
        // an identity rather than an approximation: multiComponentSolidMixture
        // mixes as 1/rho_ = sum_i Ys_i/rho_i wherever sum_i Ys_i = 1, and
        // deriveYiFromYm() has just established that in every cell holding
        // solid. Giving por a transport equation of its own makes two fields
        // out of one quantity, and the limiter in div(phiSolid) is nonlinear:
        // it resolves the por profile and the Ym profile differently, so the
        // two contradict each other at any sharp front - a cell can report
        // por = 1 while holding hundreds of kg/m3 of solid.
        //
        // rho_ is current at this point because postSolveEnergy() has called
        // solidThermo_.correct() and this is the last stage of evolveRegion().
        // Called any earlier, this would divide by a one-stage-stale density.
        //
        // Only the internal field is assigned. The patch values are left to
        // correctBoundaryConditions() below, so a fixedValue porosity patch
        // keeps the value its case prescribes.
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

        if (failOnInvalidSolidState_)
        {
            // Checked before the "< 0.0001 -> 0" clip below, which would
            // otherwise absorb an undershoot without trace. por > 1 now takes
            // negative solid mass, which solveSpeciesMass() clips away; por
            // below zero is a cell packed past solid by the transport, which
            // no discretisation of the conservative form bounds on its own.
            const label badCell = firstInvalidCell
            (
                por,
                -solidStateTolerance_,
                1.0 + solidStateTolerance_
            );

            if (badCell != -1)
            {
                OStringStream context;
                context
                    << "    Us           = " << Us_[badCell] << nl
                    << "    div(phiUs)   = "
                    << fvc::div(phiUs)()[badCell] << nl
                    << "    sum(Ym)      = " << totalYm[badCell] << nl
                    << "    rho          = " << rho_[badCell] << nl
                    << "    whereIs      = " << whereIs_[badCell];

                reportInvalidSolidState
                (
                    "the porosity recovery in recoverPorosity()",
                    por,
                    badCell,
                    context.str()
                );
            }
        }

        Info<< "porosity recovered from solid mass. Chemistry source (not"
            << " applied) min/max   = " << gMin(porositySource_)
            << ", " << gMax(porositySource_);

        Info<< "; values min Y = " << gMin(por)
            <<" max Y = " << gMax(por) << endl;

        scalar minTs = 0;

        FIFOStack<label> candidateStack = {};

        volVectorField whereIsGrad = fvc::grad(whereIs_);        

        forAll(porosity_,cellI)
        {
            if (porosity_[cellI] > critPorosity_) 
            {
                if ( (mag(whereIsGrad[cellI]) == 0) || ((mag(whereIsGrad[cellI]) > 0) && ((Us_[cellI] & whereIsGrad[cellI]) > 0))  )
                {
                    if (porosity_[cellI] < 1.0)
                    {
                        candidateStack.push(cellI);
                    }
                }
            }
            if (porosity_[cellI] < 0.0001)
            {
                porosity_[cellI] = 0.0;
                Info << "porosity 0 in cell " << cellI << endl;
            }
            if (porosity_[cellI] < 1.0)
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

        // Do not erase a nearly empty cell while solid mass is still
        // entering it through the advective transport equation.
        volScalarField divPhiYm
        (
            fvc::div(phiUs, totalYm, "div(phiSolid)")
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

        label nFlips = flipStack.size();

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

        List<Field<label>> procFlipList(Pstream::nProcs());
        procFlipList[Pstream::myProcNo()] = labelList(flipStack);
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

        if (bedCollapseSwitch_)
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
                            // Transfer only the transported Ym_; Ys_ is
                            // re-derived once after the whole route loop.
                            for (label i = 0; i < Ys_.size(); ++i)
                            {
                                Ym_[i][realRoutes[routeI][stepI-1] - minLocalGlobalI] = Ym_[i][realRoutes[routeI][stepI] - minLocalGlobalI];
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
                                         // Transfer only the transported Ym_;
                                         // Ys_ is re-derived after the loop.
                                         for (label i = 0; i < Ys_.size(); ++i)
                                         {
                                             Ym_[i][realRoutes[routeI][stepI-1] - minLocalGlobalI] = Ym_[i].boundaryField()[patchID].patchNeighbourField()()[faceID];
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

                // Bed motion moved Ym_ between cells; rebuild the mass
                // fractions Ys_ from the relocated mass concentrations.
                deriveYiFromYm();
            }
        }
        else
        {
            // this is to set porosity 0 and other fields on flipped fileds
            // it will be an alternative to the motion procedure at some point
            List<label> flipStackList = labelList(flipStack);
            forAll(flipStackList,entI)
            {
                porosity_[flipStackList[entI]] = 1.0;
                T_[flipStackList[entI]] = minTs;
            }
        }

        forAll(porosity_,cellI)
        {
            if (porosity_[cellI] < 0.0001)
            {
                porosity_[cellI] = 0.0;
                Info << "porosity 0 in cell " << cellI << endl;
            }
            if (porosity_[cellI] < 1.0)
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

        // The invariant the recovery establishes, re-checked per cell after
        // everything above that writes porosity_ directly: the bed-motion
        // model, the "< 1e-4 -> 0" clip, and the flip of a crit-porosity cell
        // to 1. Each of those can leave a porosity the cell's own solid mass
        // contradicts, and a field-wide min/max cannot see it - the defect is
        // a disagreement between two fields, not an out-of-range value in
        // either. That is precisely how a cell reporting porosity = 1 while
        // holding 252 kg/m3 of solid stayed invisible for months, so the
        // worst cell is reported every step.
        {
            scalar maxResidual = 0.0;
            label worstCell = -1;

            forAll(porosity_, cellI)
            {
                scalar cellYm = 0.0;
                forAll(Ym_, i)
                {
                    cellYm += Ym_[i][cellI];
                }

                const scalar residual = mag
                (
                    1.0 - porosity_[cellI] - cellYm/max(rho_[cellI], SMALL)
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

            if
            (
                globalResidual > solidStateTolerance_
             && worstCell != -1
             && maxResidual == globalResidual
            )
            {
                WarningInFunction
                    << "porosity and solid mass disagree by " << maxResidual
                    << " in cell " << worstCell << " at "
                    << mesh_.C()[worstCell] << ": porosity = "
                    << porosity_[worstCell] << ", whereIs = "
                    << whereIs_[worstCell]
                    << ". A porosity written after the recovery cannot be"
                    << " reconciled with the mass the cell holds." << endl;
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

        surfaceScalarField phiUs(solidVolFlux());

        for (label i = 0; i < Ys_.size(); ++i)
        {
            volScalarField& Ym_i = Ym_[i];
            volScalarField sRhoSi = solidChemistry_->RRs(i);

            Ym_i.correctBoundaryConditions();

            surfaceScalarField phiUsI(phiUs);

            volScalarField divYmFlux
            (
                fvc::div(phiUsI, Ym_i, "div(phiSolid)")
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

            if (failOnInvalidSolidState_)
            {
                // Ym is extensive, so the admissible undershoot scales with
                // the amount of solid actually present in the field.
                const scalar YmScale = max(gMax(Ym_i), SMALL);

                const label badCell = firstInvalidCell
                (
                    Ym_i,
                    -solidStateTolerance_*YmScale,
                    GREAT
                );

                if (badCell != -1)
                {
                    OStringStream context;
                    context
                        << "    specie       = " << Ys_[i].name() << nl
                        << "    Us           = " << Us_[badCell] << nl
                        << "    div(phiUs Ym)= " << divYmFlux[badCell] << nl
                        << "    RRs          = " << sRhoSi[badCell] << nl
                        << "    porosity     = " << porosity_[badCell];

                    reportInvalidSolidState
                    (
                        "the solid specie mass equation in solveSpeciesMass()",
                        Ym_i,
                        badCell,
                        context.str()
                    );
                }
            }

            // Mass fabricated by the clip, charged to the budget below so a
            // conserved-looking total cannot hide a clipped undershoot.
            cumulativeYmClip_ +=
                gSum(max(-Ym_i.field(), 0.0)*mesh_.V());

            Ym_i.max(0.0);                       // mass concentration >= 0

            Info<< "solid " << Ys_[i].name()
                << " equation solved. Sources min/max   = " << gMin(sRhoSi)
                << ", " << gMax(sRhoSi)
                << "; values min Ym = " << gMin(Ym_[i])
                << " max Ym = " << gMax(Ym_[i]) << endl;
        }

        deriveYiFromYm();

        scalar totalYmMass = 0.0;
        for (label i = 0; i < Ym_.size(); ++i)
        {
            totalYmMass += gSum(Ym_[i].field() * mesh_.V());
        }

        Info<< "solid mass budget: sum(Ym_i * V) = " << totalYmMass
            << ", initial = " << initialTotalYmMass_
            << ", fabricated by clip = " << cumulativeYmClip_ << endl;


        for (label i = 0; i < Ys_.size(); ++i)
        {
            Info<< "       derived " << Ys_[i].name()
                << " min/max = " << gMin(Ys_[i]) << " / " << gMax(Ys_[i]) << endl;
        }
    }
}

void volPyrolysis::preSolveEnergy()
{
// eqZx2uHGn047

    if (debug)
    {
        Info<< "volPyrolysis::preSolveEnergy()" << endl;
    }

    if (active_)
    {

        volTensorField composedK(K_ * (1 - porosity_) * anisotropyK_);
        radiationSh_ = radiation_;

        if (equilibrium_)
        {}
        else
        {

            // Refresh the solid mass boundary values before anything is
            // derived from them: fixedYm evaluates Yi*rho*(1-porosityF), and
            // both rho and porosityF were last updated at the end of the
            // previous step. solveSpeciesMass() corrects them too, so this
            // only moves the correction earlier.
            forAll(Ym_, i)
            {
                Ym_[i].correctBoundaryConditions();
            }

            volScalarField totalYm = 0*Ym_[0];
            for (label i = 0; i < Ys_.size(); ++i)
            {
                totalYm += Ym_[i];
            }
            // The heat capacity of the solid the cell actually holds.
            // Masking it with whereIs_ collapsed it to the floor in any cell
            // whose mask and mass disagreed, while solidH_ carried no such
            // factor - which is what turned that disagreement into a
            // temperature of 1e23 K. postSolveEnergy() builds the same
            // quantity unmasked.
            volScalarField rhoCp
            (
                max
                (
                    totalYm * solidThermo_.Cp(),
                    dimensionedScalar("minRhoCp",dimEnergy/dimTemperature/dimVolume,SMALL)
                )
            );

            T_ = solidH_()/rhoCp;

            if (failOnInvalidSolidState_)
            {
                // solidH_ and totalYm are advected by separate explicit
                // equations with separate limiters, so nothing keeps their
                // ratio physical once a cell loses its solid inventory.
                const label badCell = firstInvalidCell
                (
                    T_,
                    SMALL,
                    maxSolidTemperature_,
                    &totalYm.primitiveField()
                );

                if (badCell != -1)
                {
                    OStringStream context;
                    context
                        << "    solidH       = "
                        << solidH_()[badCell] << nl
                        << "    sum(Ym)      = " << totalYm[badCell] << nl
                        << "    rhoCp        = " << rhoCp[badCell] << nl
                        << "    porosity     = " << porosity_[badCell] << nl
                        << "    whereIs      = " << whereIs_[badCell];

                    reportInvalidSolidState
                    (
                        "Ts = solidH/rhoCp at the head of preSolveEnergy()",
                        T_,
                        badCell,
                        context.str()
                    );
                }
            }

            T_.correctBoundaryConditions();

            whereIs_.correctBoundaryConditions();
            whereIsNot_.correctBoundaryConditions();
            surfaceScalarField  whereIsPatch  = fvc::interpolate(whereIs_);

            volScalarField heatTransfField = whereIs_*heatTransfer()()*pos(critPorosity_ - porosity_);

            // Simplistic immersed boundary for heat transport in solid phase.
            fvScalarMatrix TLap
            (
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
               if (isA<processorPolyPatch>(mesh_.boundaryMesh()[patchI]) or isA<cyclicPolyPatch>(mesh_.boundaryMesh()[patchI]) or isA<processorCyclicPolyPatch>(mesh_.boundaryMesh()[patchI]) )
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

            fvScalarMatrix TEqn
            (
                fvm::ddt(rhoCp,T_)
              - TLap                                                  
            ==
                chemistrySh_ // eqZx2uHGn004, eqZx2uHGn017
              - heatTransfField // eqZx2uHGn005
              - heatUpGas_
              + radiationSh_
            );

            TEqn.relax();
            TEqn.solve();

            if (failOnInvalidSolidState_)
            {
                const label badCell = firstInvalidCell
                (
                    T_,
                    SMALL,
                    maxSolidTemperature_,
                    &totalYm.primitiveField()
                );

                if (badCell != -1)
                {
                    OStringStream context;
                    context
                        << "    rhoCp        = " << rhoCp[badCell] << nl
                        << "    chemistrySh  = "
                        << chemistrySh_[badCell] << nl
                        << "    heatTransfer = "
                        << heatTransfField[badCell] << nl
                        << "    heatUpGas    = " << heatUpGas_[badCell] << nl
                        << "    radiationSh  = "
                        << radiationSh_[badCell] << nl
                        << "    porosity     = " << porosity_[badCell];

                    reportInvalidSolidState
                    (
                        "the solid energy equation in preSolveEnergy()",
                        T_,
                        badCell,
                        context.str()
                    );
                }
            }

            volScalarField patchedSolidH = (rhoCp*T_);
            solidH_().ref() = patchedSolidH;
            solidH_().correctBoundaryConditions();

            surfaceScalarField solidFlux(solidVolFlux());

            // Move the solid enthalpy with the solid MASS rather than with the
            // solid volume. Both used to ride solidFlux under the same
            // div(phiSolid) limiter, which is not the same thing as riding it
            // in lockstep: the limiter is nonlinear, so it resolves the solidH
            // profile and the Ym profile with different face values, and the
            // arriving mass does not carry the enthalpy that belongs to it.
            // Measured in charOnlyMoveCases/serial: a cell takes its solid one
            // step before its enthalpy, so Ts = solidH/rhoCp reads 0 K there
            // and then climbs 0 -> 28.7 -> 130.8 -> 181.5 K.
            //
            // phiYm is assembled from exactly the per-specie face fluxes that
            // solveSpeciesMass() integrates a few lines later - same flux, same
            // start-of-step Ym, same div(phiSolid) scheme - so the mass that
            // leaves a face here is the mass that leaves it there.
            surfaceScalarField phiYm
            (
                fvc::flux(solidFlux, Ym_[0], "div(phiSolid)")
            );
            for (label i = 1; i < Ym_.size(); ++i)
            {
                phiYm += fvc::flux(solidFlux, Ym_[i], "div(phiSolid)");
            }

            // The specific enthalpy of the solid a cell holds [J/kg], upwinded
            // with respect to that mass flux, so mass arrives carrying its
            // donor's specific enthalpy. A receiving cell then holds
            //
            //     (solidH + m_in*hs_donor) / (totalYm + m_in)
            //
            // which is a convex combination of its own temperature and its
            // donors', while a donating cell sheds enthalpy and mass in the
            // same ratio and keeps its temperature exactly. Ts is bounded by
            // the temperatures already present, whatever the mass limiter does.
            //
            // Deliberately upwind and not div(phiSolid): a limiter on hs would
            // sharpen the thermal front, but it would also produce face values
            // outside the donor-receiver range, which is the defect above. The
            // price is a thermal front more diffuse than the mass front.
            // The floor is the smallest mass concentration distinguishable
            // from zero: rho_ is the skeletal density, so a cell holding less
            // than solidStateTolerance_ of a fully packed cell's mass holds
            // nothing, and pos() switches its enthalpy flux off entirely. No
            // mass, no enthalpy carried - which also makes this robust to a
            // case that prescribes solidH inconsistently with Ym on a patch
            // (charOnlyMoveCases/solidInlet does, and the raw ratio there is
            // 1e20 J/kg). The enthalpy withheld is bounded by the mass that
            // was below the floor, so it is below tolerance by construction.
            // Floored by SMALL as well: rho_ is zero in a cell that has never
            // held solid (deriveYiFromYm() leaves its mass fractions at zero,
            // and the mixture rule then gives no density), and an unfloored
            // 0/0 here is a SIGFPE, not a large number.
            const volScalarField YmFloor
            (
                max
                (
                    solidStateTolerance_*rho_,
                    dimensionedScalar("YmFloorMin", dimDensity, SMALL)
                )
            );

            volScalarField hs
            (
                solidH_()*pos(totalYm - YmFloor)/max(totalYm, YmFloor)
            );

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

            if (failOnInvalidSolidState_)
            {
                const volScalarField& sH = solidH_();
                const scalar sHScale = max(gMax(sH), SMALL);

                const label badCell = firstInvalidCell
                (
                    sH,
                    -solidStateTolerance_*sHScale,
                    GREAT
                );

                if (badCell != -1)
                {
                    OStringStream context;
                    context
                        << "    Us           = " << Us_[badCell] << nl
                        << "    Ts           = " << T_[badCell] << nl
                        << "    rhoCp        = " << rhoCp[badCell] << nl
                        << "    sum(Ym)      = " << totalYm[badCell] << nl
                        << "    porosity     = " << porosity_[badCell];

                    reportInvalidSolidState
                    (
                        "the solid enthalpy advection in preSolveEnergy()",
                        sH,
                        badCell,
                        context.str()
                    );
                }
            }

            solidH_().max(0);
            solidH_().correctBoundaryConditions();

        }
    }
}

void volPyrolysis::postSolveEnergy()
{
// eqZx2uHGn047

    if (debug)
    {
        Info<< "volPyrolysis::postSolveEnergy()" << endl;
    }

    if (active_)
    {

        if (equilibrium_)
        {}
        else
        {
            volScalarField totalYm = 0*Ym_[0];
            for (label i = 0; i < Ys_.size(); ++i)
            {
                totalYm += Ym_[i];
            }
            solidThermo_.correct(); 
            volScalarField rhoCp
            (
                max
                (
                    totalYm * solidThermo_.Cp(),
                    dimensionedScalar("minRhoCp",dimEnergy/dimTemperature/dimVolume,SMALL)
                )
            );
            volScalarField weight = critPorosity_ - porosity_;
            T_ = whereIs_*(solidH_()/rhoCp*pos(weight) + gasThermo_.T()*neg(weight));

            if (failOnInvalidSolidState_)
            {
                // whereIs_ zeroes T_ in gas-only cells, so the lower bound
                // has to admit zero here.
                const label badCell =
                    firstInvalidCell(T_, 0.0, maxSolidTemperature_);

                if (badCell != -1)
                {
                    OStringStream context;
                    context
                        << "    solidH       = "
                        << solidH_()[badCell] << nl
                        << "    sum(Ym)      = " << totalYm[badCell] << nl
                        << "    rhoCp        = " << rhoCp[badCell] << nl
                        << "    porosity     = " << porosity_[badCell] << nl
                        << "    whereIs      = " << whereIs_[badCell];

                    reportInvalidSolidState
                    (
                        "Ts = whereIs*solidH/rhoCp in postSolveEnergy()",
                        T_,
                        badCell,
                        context.str()
                    );
                }
            }

            T_.correctBoundaryConditions();

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

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

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
    T_(solidThermo_.T()),
    solidH_(nullptr),
    equilibrium_(false),
    subintegrateSwitch_(false),
    bedCollapseSwitch_(false),
    replenishSwitch_(false),
    advectSolidFields_(true),
    failOnInvalidSolidState_(true),
    solidStateTolerance_(1e-8),
    maxSolidTemperature_(1e5),
    critPorosity_(0.9999),
    poroProtectSolidInflowFluxTolerance_(1e-12),
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
    timeChem_(1.0),
    initialTotalYmMass_(0.0),
    cumulativeYmOutflow_(0.0),
    cumulativeYmClip_(0.0)
{

    mesh.setFluxRequired(T_.name());

    ST_ = STmodel_->ST()();
    CONV_ = CONV();
    rho0_.ref() = rho_.ref();

    subintegrateSwitch_ = coeffs().lookupOrDefault("subintegrateHeatTransfer",false);
    bedCollapseSwitch_ = coeffs().lookupOrDefault("bedCollapse",false);
    replenishSwitch_ = coeffs().lookupOrDefault("replenish",false);
    advectSolidFields_ = coeffs().lookupOrDefault("advectSolidFields",true);
    failOnInvalidSolidState_ =
        coeffs().lookupOrDefault("failOnInvalidSolidState",true);
    solidStateTolerance_ =
        coeffs().lookupOrDefault<scalar>("solidStateTolerance",1e-8);
    maxSolidTemperature_ =
        coeffs().lookupOrDefault<scalar>("maxSolidTemperature",1e5);
    critPorosity_ = coeffs().lookupOrDefault("criticalPorosity",0.9999);
    poroProtectSolidInflowFluxTolerance_ =
        coeffs().lookupOrDefault
        (
            "poroProtectSolidInflowFluxTolerance",
            1e-12
        );

    Info << endl;
    Info << "subintegrateHeatTransfer " << subintegrateSwitch_ << endl;
    Info << "bedCollapse              " << bedCollapseSwitch_    << endl;
    if (bedCollapseSwitch_) 
    {
        Info << "criticalPorosity         " << critPorosity_  << endl;
    }
    Info << "poroProtectSolidInflowFluxTolerance  "
         << poroProtectSolidInflowFluxTolerance_
         << " [kg/m3/s]" << endl;
    Info << "replenish                " << replenishSwitch_    << endl;
    Info << "advectSolidFields        " << advectSolidFields_  << endl;
    Info << "failOnInvalidSolidState  " << failOnInvalidSolidState_ << endl;
    Info << "solidStateTolerance      " << solidStateTolerance_ << endl;
    Info << "maxSolidTemperature      " << maxSolidTemperature_ << endl;
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

    whereIs_ = neg(porosity_ - 1);
    whereIsNot_ = pos0(porosity_ - 1);

    // porosity_ is assigned in recoverPorosity(), not solved, and
    // GeometricField::storeOldTimes() rolls a field forward only once an
    // old-time field exists - it silently does nothing the first time, while
    // still marking the field as current. Create the old-time field here,
    // from the initial state, or the first assignment leaves porosityF_0
    // equal to the value just written and the gas-side
    // fvm::ddt(porosityF, rho) loses a whole step of d(porosity)/dt.
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
        if (whereIs_[cellI] == 1)
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
        if ( active_ && whereIs_[cellI] != 0)
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

    preSolveEnergy(); 
    solveSpeciesMass(); 
    postSolveEnergy();

    // Last: recoverPorosity() divides the solid mass by rho_, which
    // postSolveEnergy()'s solidThermo_.correct() has just refreshed.
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
            volScalarField rhoCpS
                (
                        max
                        (
                            rho_ * solidThermo_.Cp() * (1 - porosity_),
                            dimensionedScalar("minRhoCp",dimEnergy/dimTemperature/dimVolume,SMALL)
                        )
                );
            volScalarField deltaTemp(T_ * 0);
            scalar deltaTime = time_.deltaTValue();

            forAll(deltaTemp,cellI)
            {
                if (whereIs_[cellI] == 1.0 && CONV_[cellI] > 0.0)
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
