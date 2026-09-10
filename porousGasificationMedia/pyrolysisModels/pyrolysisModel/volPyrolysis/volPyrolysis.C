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

void volPyrolysis::recoverPorosity()
{
    if (active_)
    {
        porositySource_ = solidChemistry_->RRpor(T_)();

        volScalarField& por = porosity_;

        const surfaceScalarField& phiUs = phiSolid_;

        volScalarField totalYm = 0*Ym_[0];

        forAll(Ym_, i)
        {
            totalYm += Ym_[i];
        }

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

        // Before the "< 1e-4 -> 0" clip below, which would absorb an
        // undershoot without trace.
        const label badCell = solidStateChecker_->firstInvalidPorosity(por);

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

            solidStateChecker_->abort
            (
                "the porosity recovery in recoverPorosity()",
                por,
                badCell,
                context.str()
            );
        }

        Info<< "porosity recovered from solid mass. Chemistry source (not"
            << " applied) min/max   = " << gMin(porositySource_)
            << ", " << gMax(porositySource_);

        Info<< "; values min Y = " << gMin(por)
            <<" max Y = " << gMax(por) << endl;

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
                scalar collapsedMass = 0;
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
                if (collapseMovesSolidMass_)
                {
                    // The solid enthalpy travels with the mass across a
                    // processor boundary too, so its neighbour values have
                    // to be current before any route is walked.
                    solidH_().correctBoundaryConditions();
                }
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
                    if
                    (
                        collapseMovesSolidMass_
                     && (minLocalGlobalI <= realRoutes[routeI][0])
                     && (realRoutes[routeI][0] <= maxLocalGlobalI)
                     && (realRoutes[routeI].size() > 1 || !replenishSwitch_)
                    )
                    {
                        // The route's foot is where the collapse was called;
                        // what it holds leaves the domain (or stays, if the
                        // route is length one and replenishing). Charged here.
                        const label footCell =
                            realRoutes[routeI][0] - minLocalGlobalI;

                        forAll(Ym_, i)
                        {
                            collapsedMass +=
                                Ym_[i][footCell]*cellVolume_[footCell];
                        }
                    }
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
                            // Ym_ (mass per unit volume) takes the same
                            // ratio; solidH_ travels with it too.
                            if (collapseMovesSolidMass_)
                            {
                                for (label i = 0; i < Ys_.size(); ++i)
                                {
                                    Ym_[i][realRoutes[routeI][stepI-1] - minLocalGlobalI] =
                                        Ym_[i][realRoutes[routeI][stepI] - minLocalGlobalI]*cellVolumeRatio;
                                }

                                solidH_()[realRoutes[routeI][stepI-1] - minLocalGlobalI] =
                                    solidH_()[realRoutes[routeI][stepI] - minLocalGlobalI]*cellVolumeRatio;
                            }
                            else
                            {
                                for (label i = 0; i < Ys_.size(); ++i)
                                {
                                    Ym_[i][realRoutes[routeI][stepI-1] - minLocalGlobalI] = Ym_[i][realRoutes[routeI][stepI] - minLocalGlobalI];
                                }
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
                                            // Pass values from one subdomain to another.
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
                                         // Ratio and enthalpy as in the
                                         // on-processor branch above.
                                         if (collapseMovesSolidMass_)
                                         {
                                             for (label i = 0; i < Ys_.size(); ++i)
                                             {
                                                 Ym_[i][realRoutes[routeI][stepI-1] - minLocalGlobalI] = Ym_[i].boundaryField()[patchID].patchNeighbourField()()[faceID]*cellVolumeRatio;
                                             }
                                             solidH_()[realRoutes[routeI][stepI-1] - minLocalGlobalI] = solidH_().boundaryField()[patchID].patchNeighbourField()()[faceID]*cellVolumeRatio;
                                         }
                                         else
                                         {
                                             for (label i = 0; i < Ys_.size(); ++i)
                                             {
                                                 Ym_[i][realRoutes[routeI][stepI-1] - minLocalGlobalI] = Ym_[i].boundaryField()[patchID].patchNeighbourField()()[faceID];
                                             }
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
                            // Nothing falls in from above the route's top, so
                            // under collapseMovesSolidMass_ the mass fields
                            // are zeroed too; not charged (copied out above).
                            if (collapseMovesSolidMass_)
                            {
                                const label topCell = realRoutes[routeI][realRoutes[routeI].size() - 1] - minLocalGlobalI;

                                forAll(Ym_, i)
                                {
                                    Ym_[i][topCell] = 0.0;
                                }

                                solidH_()[topCell] = 0.0;
                                T_[topCell] = 0.0;
                            }

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
                reduce(collapsedMass, sumOp<scalar>());
                cumulativeCollapseMass_ += collapsedMass;
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
        else if (emptyFlippedCells_)
        {
            // Emptying a cell removes its solid outright rather than just
            // rewriting porosity; the mass is destroyed (no destination) and
            // charged to cumulativeFlipMass_. T_ zeroed to match solidH_.
            const scalarField& V = mesh_.V();
            scalar flippedMass = 0.0;

            List<label> flipStackList = labelList(flipStack);
            forAll(flipStackList,entI)
            {
                const label cellI = flipStackList[entI];

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
                // bedCollapse branch does after relocating mass.
                deriveYiFromYm();
            }
        }

        label nZeroPorosity = 0;

        forAll(porosity_,cellI)
        {
            if (porosity_[cellI] < 0.0001)
            {
                porosity_[cellI] = 0.0;
                ++nZeroPorosity;
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

        if (nZeroPorosity)
        {
            Info << "porosity 0 in " << nZeroPorosity << " cells" << endl;
        }

        // Here, after everything above that writes porosity_ directly: the
        // bed-motion model, the "< 1e-4 -> 0" clip, and the flip to por = 1.
        solidStateChecker_->checkConsistency(porosity_, Ym_, rho_, whereIs_);

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

        // The already-limited flux, not what Us asked for: settled once per
        // step so every solid field rides the same value at each face.
        const surfaceScalarField& phiUs = phiSolid_;

        // Reset lambdaDot_, then add its temperature-driven term; the
        // chemistry-driven part accumulates per specie below.
#ifdef WITH_YADE
        if (demActive_)
        {
            lamDotCalc_->beginStep();
            lamDotCalc_->calculateTemperatureDriven();
        }
#endif

        for (label i = 0; i < Ys_.size(); ++i)
        {
            volScalarField& Ym_i = Ym_[i];
            volScalarField sRhoSi = solidChemistry_->RRs(i);

            // Chemistry-driven lambdaDot term for this specie; a no-op under
            // lambdaMode constant. lambdaDot only reads sRhoSi, which the Ym
            // equation below uses unchanged. The name passed is the bare
            // solidComponents one ("char", not "Ychar"), as a per-specie
            // dlambdaOverDYmi subdict is keyed that way.
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

            const label badCell =
                solidStateChecker_->firstInvalidExtensive(Ym_i);

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
                    Ym_i,
                    badCell,
                    context.str()
                );
            }

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

        // lambdaDot is complete for this step once every specie has
        // contributed.
#ifdef WITH_YADE
        if (demActive_)
        {
            lambdaDotPtr_->correctBoundaryConditions();
        }
#endif

        scalar totalYmMass = 0.0;
        for (label i = 0; i < Ym_.size(); ++i)
        {
            totalYmMass += gSum(Ym_[i].field() * mesh_.V());
        }

        Info<< "solid mass budget: sum(Ym_i * V) = " << totalYmMass
            << ", initial = " << initialTotalYmMass_
            << ", fabricated by clip = " << cumulativeYmClip_
            << ", destroyed by flip = " << cumulativeFlipMass_
            << ", destroyed by collapse = " << cumulativeCollapseMass_
            << endl;


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

            forAll(Ym_, i)
            {
                Ym_[i].correctBoundaryConditions();
            }

            volScalarField totalYm = 0*Ym_[0];
            for (label i = 0; i < Ys_.size(); ++i)
            {
                totalYm += Ym_[i];
            }

            // What the two Ts guards below are judged on.
            const volScalarField solidPresent
            (
                solidStateChecker_->solidPresent(totalYm, rho_)
            );

            // The heat capacity of the solid the cell actually holds. No
            // whereIs_ factor: solidH_ carries none either, and masking one
            // and not the other is a temperature of any size at all.
            volScalarField rhoCp
            (
                max
                (
                    totalYm * solidThermo_.Cp(),
                    dimensionedScalar("minRhoCp",dimEnergy/dimTemperature/dimVolume,SMALL)
                )
            );

            T_ = solidH_()/rhoCp;

            const label badTsCell =
                solidStateChecker_->firstInvalidTemperature
                (
                    T_,
                    solidPresent.primitiveField()
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
                    << "    whereIs      = " << whereIs_[badTsCell];

                solidStateChecker_->abort
                (
                    "Ts = solidH/rhoCp at the head of preSolveEnergy()",
                    T_,
                    badTsCell,
                    context.str()
                );
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

            const label badTEqnCell =
                solidStateChecker_->firstInvalidTemperature
                (
                    T_,
                    solidPresent.primitiveField()
                );

            if (badTEqnCell != -1)
            {
                OStringStream context;
                context
                    << "    rhoCp        = " << rhoCp[badTEqnCell] << nl
                    << "    chemistrySh  = "
                    << chemistrySh_[badTEqnCell] << nl
                    << "    heatTransfer = "
                    << heatTransfField[badTEqnCell] << nl
                    << "    heatUpGas    = " << heatUpGas_[badTEqnCell] << nl
                    << "    radiationSh  = "
                    << radiationSh_[badTEqnCell] << nl
                    << "    porosity     = " << porosity_[badTEqnCell];

                solidStateChecker_->abort
                (
                    "the solid energy equation in preSolveEnergy()",
                    T_,
                    badTEqnCell,
                    context.str()
                );
            }

            volScalarField patchedSolidH = (rhoCp*T_);
            solidH_().ref() = patchedSolidH;
            solidH_().correctBoundaryConditions();

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
                    "the solid enthalpy advection in preSolveEnergy()",
                    sH,
                    badSolidHCell,
                    context.str()
                );
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
            // Same ratio preSolveEnergy() reads back as T_.oldTime(), so the
            // two must agree. No whereIs_ factor: it lags a stage, and
            // zeroing T_ there while keeping enthalpy would start at 0 K.
            if (gasTemperatureBelowCriticalPorosity_)
            {
                // Emptier than critPorosity_, the cell follows the gas. pos0
                // and neg, not pos and neg: pos(0)=neg(0)=0 would otherwise
                // leave a cell exactly at critPorosity_ with Ts = 0.
                const volScalarField weight(critPorosity_ - porosity_);

                T_ = solidH_()/rhoCp*pos0(weight)
                   + gasThermo_.T()*neg(weight);
            }
            else
            {
                T_ = solidH_()/rhoCp;
            }

            const label badCell =
                solidStateChecker_->firstInvalidTemperature(T_);

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

                solidStateChecker_->abort
                (
                    "the Ts recovery in postSolveEnergy()",
                    T_,
                    badCell,
                    context.str()
                );
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
    collapseMovesSolidMass_(false),
    emptyFlippedCells_(false),
    gasTemperatureBelowCriticalPorosity_(false),
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
    cumulativeYmOutflow_(0.0),
    cumulativeYmClip_(0.0),
    cumulativeFlipMass_(0.0),
    cumulativeCollapseMass_(0.0)
{

    mesh.setFluxRequired(T_.name());

    ST_ = STmodel_->ST()();
    CONV_ = CONV();
    rho0_.ref() = rho_.ref();

    subintegrateSwitch_ = coeffs().lookupOrDefault("subintegrateHeatTransfer",false);
    bedCollapseSwitch_ = coeffs().lookupOrDefault("bedCollapse",false);
    replenishSwitch_ = coeffs().lookupOrDefault("replenish",false);
    collapseMovesSolidMass_ =
        coeffs().lookupOrDefault("collapseMovesSolidMass",false);
    emptyFlippedCells_ =
        coeffs().lookupOrDefault("emptyFlippedCells",false);
    gasTemperatureBelowCriticalPorosity_ =
        coeffs().lookupOrDefault("gasTemperatureBelowCriticalPorosity",false);
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
    if (bedCollapseSwitch_ || gasTemperatureBelowCriticalPorosity_)
    {
        Info << "criticalPorosity         " << critPorosity_  << endl;
    }
    Info << "poroProtectSolidInflowFluxTolerance  "
         << poroProtectSolidInflowFluxTolerance_
         << " [kg/m3/s]" << endl;
    Info << "replenish                " << replenishSwitch_    << endl;
    Info << "collapseMovesSolidMass   " << collapseMovesSolidMass_ << endl;
    Info << "emptyFlippedCells        " << emptyFlippedCells_  << endl;
    Info << "gasTemperatureBelowCriticalPorosity  "
         << gasTemperatureBelowCriticalPorosity_ << endl;
    // Constructed here because it carries the last three banner lines.
    solidStateChecker_.reset(new SolidStateChecker(coeffs(), mesh_, time_));

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
            active_
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

    whereIs_ = neg(porosity_ - 1);
    whereIsNot_ = pos0(porosity_ - 1);

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

    // Settle how much solid each face may carry before any solid field is
    // transported. Needs the chemistry rates, so it follows their solve.
    limitSolidVolFlux();

    preSolveEnergy(); 
    solveSpeciesMass(); 
    postSolveEnergy();

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
            // Keep the subintegrated gas/solid exchange defined in a fully
            // solid cell. The physical gas capacity vanishes there, while
            // the finite floor prevents an ill-conditioned analytic update.
            volScalarField rhoCpG
            (
                gasThermo_.rho()
              * gasThermo_.Cp()
              * max
                (
                    porosity_,
                    dimensionedScalar("gasPorosityFloor", dimless, 1e-4)
                )
            );
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
