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

#include "bedCollapse.H"

#include "surfaceFields.H"
#include "processorPolyPatch.H"
#include "ListListOps.H"
#include "PstreamReduceOps.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //

tmp<volScalarField> BedCollapse::globalCellNumbering
(
    List<Field<scalar>>& globalIndex
) const
{
    globalIndex[Pstream::myProcNo()] =
        solidOccupiedCellLastStep_.internalField();
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

    tmp<volScalarField> tglobalIndices(solidState_.solidPresent()*0);
    volScalarField& globalIndices = tglobalIndices.ref();
    forAll(globalIndices,cellI)
    {
        globalIndices[cellI] = globalIndex[Pstream::myProcNo()][cellI];
    }
    globalIndices.correctBoundaryConditions();

    return tglobalIndices;
}

scalarField BedCollapse::upperSolidNeighbour
(
    const volScalarField& globalIndices
) const
{
    // The face above the cell centre (+z) is the one solid falls through;
    // a gravity-aligned choice that is unique per cell is still open.
    const scalar upperFaceTolerance = 1e-6;

    const volScalarField& solidPresent = solidState_.solidPresent();

    scalarField takeFrom(solidPresent.primitiveField());
    forAll(mesh_.cells(),cellI)
    {
        if (solidPresent[cellI] < 1)
        {
            takeFrom[cellI] = -1;
            continue;
        }

        label takeFromGlobalID = -1;
        const cell& cellFaces = mesh_.cells()[cellI];
        forAll(cellFaces,faceI)
        {
            if
            (
                mesh_.Cf()[cellFaces[faceI]].z()
              > mesh_.C()[cellI].z() + upperFaceTolerance
            )
            {
                const label patchID =
                    mesh_.boundaryMesh().whichPatch(cellFaces[faceI]);
                if (patchID > -1)
                {
                    const label faceID =
                        mesh_.boundaryMesh()[patchID].whichFace(cellFaces[faceI]);
                    if (isA<processorPolyPatch>(mesh_.boundaryMesh()[patchID]))
                    {
                        takeFromGlobalID = globalIndices.boundaryField()[patchID].patchNeighbourField()()[faceID];
                        if (solidPresent.boundaryField()[patchID].patchNeighbourField()()[faceID] < 1)
                        {
                            takeFromGlobalID = -1;
                        }
                    }
                }
                else
                {
                    if (cellI == mesh_.faceNeighbour()[cellFaces[faceI]])
                    {
                        takeFromGlobalID = mesh_.faceOwner()[cellFaces[faceI]];
                    }
                    else
                    {
                        takeFromGlobalID = mesh_.faceNeighbour()[cellFaces[faceI]];
                    }
                    if (solidPresent[takeFromGlobalID] < 1)
                    {
                        takeFromGlobalID = -1;
                    }
                    else
                    {
                        takeFromGlobalID = globalIndices[takeFromGlobalID];
                    }
                }
            }
        }
        takeFrom[cellI] = takeFromGlobalID;
    }

    return takeFrom;
}

List<List<label>> BedCollapse::collapseRoutes
(
    const List<Field<label>>& procFlipList,
    const List<Field<scalar>>& globalIndex,
    const scalarField& takeFrom
) const
{
    List<Field<scalar>> routes(Pstream::nProcs());
    routes[Pstream::myProcNo()] = takeFrom;
    Pstream::gatherList(routes);

    // The master follows takeFrom upward from each flipped cell until no
    // solid lies above; every rank then walks the routes it holds cells of.
    List<List<label>> realRoutes = {};
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
                label prevStep = globalIndex[lI][procFlipList[lI][entI]];
                List<label> realRoute = {prevStep};
                while (routesCombined[prevStep] >= 0)
                {
                    prevStep = routesCombined[prevStep];
                    realRoute.append(prevStep);
                }
                realRoutes.append(realRoute);
            }
        }
    }
    Pstream::scatter(realRoutes);

    return realRoutes;
}

void BedCollapse::syncCollapseBoundaries()
{
    solidOccupiedCellLastStep_ = solidOccupiedCellLastStep_*0;
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
}

BedCollapse::MovedSolid BedCollapse::localSolid(const label cellI) const
{
    MovedSolid solid;
    solid.porosity = porosity_[cellI];
    solid.porosityArch = porosityArch_[cellI];
    solid.T = T_[cellI];
    solid.rho = rho_[cellI];
    solid.Ym.setSize(Ym_.size());
    forAll(Ym_, i)
    {
        solid.Ym[i] = Ym_[i][cellI];
    }
    solid.H = collapseMovesSolidMass_ ? solidH_()[cellI] : 0.0;

    return solid;
}

BedCollapse::MovedSolid BedCollapse::neighbourSolid
(
    const label patchID,
    const label faceID
) const
{
    MovedSolid solid;
    solid.porosity =
        porosity_.boundaryField()[patchID].patchNeighbourField()()[faceID];
    solid.porosityArch =
        porosityArch_.boundaryField()[patchID].patchNeighbourField()()[faceID];
    solid.T = T_.boundaryField()[patchID].patchNeighbourField()()[faceID];
    solid.rho = rho_.boundaryField()[patchID].patchNeighbourField()()[faceID];
    solid.Ym.setSize(Ym_.size());
    forAll(Ym_, i)
    {
        solid.Ym[i] =
            Ym_[i].boundaryField()[patchID].patchNeighbourField()()[faceID];
    }
    solid.H =
        collapseMovesSolidMass_
      ? solidH_().boundaryField()[patchID].patchNeighbourField()()[faceID]
      : 0.0;

    return solid;
}

void BedCollapse::moveSolidInto
(
    const label cellI,
    const MovedSolid& from,
    const scalar cellVolumeRatio
)
{
    // Guards against skewed cells (a large volume ratio); the floor itself
    // introduces an error.
    const scalar minCollapsedPorosity = 0.001;

    porosity_[cellI] =
        max(1. - (1. - from.porosity)*cellVolumeRatio, minCollapsedPorosity);
    porosityArch_[cellI] =
        max(1. - (1. - from.porosityArch)*cellVolumeRatio, minCollapsedPorosity);
    T_[cellI] = from.T;
    rho_[cellI] = from.rho;

    // Only the transported Ym_ moves; Ys_ is re-derived once after all
    // routes. Ym_ (per unit volume) takes the porosity's volume ratio.
    if (collapseMovesSolidMass_)
    {
        forAll(Ym_, i)
        {
            Ym_[i][cellI] = from.Ym[i]*cellVolumeRatio;
        }
        solidH_()[cellI] = from.H*cellVolumeRatio;
    }
    else
    {
        forAll(Ym_, i)
        {
            Ym_[i][cellI] = from.Ym[i];
        }
    }
}

void BedCollapse::moveSolidFromNeighbourProcessor
(
    const label cellI,
    const label fromGlobalI,
    const volScalarField& globalIndices
)
{
    const cell& cellFaces = mesh_.cells()[cellI];
    forAll(cellFaces,faceI)
    {
        const label patchID =
            mesh_.boundaryMesh().whichPatch(cellFaces[faceI]);
        if
        (
            patchID == -1
         || !isA<processorPolyPatch>(mesh_.boundaryMesh()[patchID])
        )
        {
            continue;
        }

        const label faceID =
            mesh_.boundaryMesh()[patchID].whichFace(cellFaces[faceI]);
        const label neighbourGlobalID =
            globalIndices.boundaryField()[patchID].patchNeighbourField()()[faceID];
        if (neighbourGlobalID != fromGlobalI)
        {
            continue;
        }

        const scalar cellVolumeRatio =
            cellVolume_.boundaryField()[patchID].patchNeighbourField()()[faceID]
          / cellVolume_[cellI];

        moveSolidInto(cellI, neighbourSolid(patchID, faceID), cellVolumeRatio);
    }
}

void BedCollapse::chargeRouteFoot
(
    const label footCell,
    scalar& collapsedMass
) const
{
    forAll(Ym_, i)
    {
        collapsedMass += Ym_[i][footCell]*cellVolume_[footCell];
    }
}

void BedCollapse::emptyRouteTop
(
    const label topCell,
    scalar& replenishedMass
)
{
    if (!replenishSwitch_)
    {
        // Nothing falls in from above the route's top, so under
        // collapseMovesSolidMass_ the mass fields are zeroed too; not
        // charged (copied out already).
        if (collapseMovesSolidMass_)
        {
            forAll(Ym_, i)
            {
                Ym_[i][topCell] = 0.0;
            }

            solidH_()[topCell] = 0.0;
            T_[topCell] = 0.0;
        }

        porosity_[topCell] = 1.;
        porosityArch_[topCell] = 1.;
    }
    replenishedMass +=
        (1. - porosity_[topCell])*rho_[topCell]*cellVolume_[topCell];
}

void BedCollapse::walkCollapseRoute
(
    const List<label>& route,
    const List<Field<scalar>>& globalIndex,
    const volScalarField& globalIndices,
    scalar& replenishedMass,
    scalar& collapsedMass
)
{
    const Field<scalar>& localGlobalIndex = globalIndex[Pstream::myProcNo()];
    const label minLocalGlobalI = localGlobalIndex[0];
    const label maxLocalGlobalI = localGlobalIndex[localGlobalIndex.size()-1];
    auto isLocal = [&](const label globalI)
    {
        return minLocalGlobalI <= globalI && globalI <= maxLocalGlobalI;
    };

    // The route's foot is where the collapse was called; what it holds
    // leaves the domain (or stays, if the route is length one and
    // replenishing).
    if
    (
        collapseMovesSolidMass_
     && isLocal(route[0])
     && (route.size() > 1 || !replenishSwitch_)
    )
    {
        chargeRouteFoot(route[0] - minLocalGlobalI, collapsedMass);
    }

    // Each step pulls the solid of the cell above into the cell below.
    bool currentI = false;
    bool previousI = false;
    for (label stepI = 0; stepI < route.size(); stepI++)
    {
        previousI = currentI;
        currentI = isLocal(route[stepI]);
        if (currentI)
        {
            solidOccupiedCellLastStep_[route[stepI] - minLocalGlobalI] = -1;
        }

        if (previousI && currentI)
        {
            const label below = route[stepI-1] - minLocalGlobalI;
            const label above = route[stepI] - minLocalGlobalI;
            moveSolidInto
            (
                below,
                localSolid(above),
                cellVolume_[above]/cellVolume_[below]
            );
        }
        if (previousI && !currentI)
        {
            moveSolidFromNeighbourProcessor
            (
                route[stepI-1] - minLocalGlobalI,
                route[stepI],
                globalIndices
            );
        }
    }

    if (isLocal(route[route.size() - 1]))
    {
        emptyRouteTop(route[route.size() - 1] - minLocalGlobalI, replenishedMass);
    }
}

void BedCollapse::reportCollapsedMass
(
    scalar replenishedMass,
    scalar collapsedMass
)
{
    reduce(replenishedMass, sumOp<scalar>());
    reduce(collapsedMass, sumOp<scalar>());
    cumulativeCollapseMass_ += collapsedMass;
    if (Pstream::master())
    {
        totRepMass_ += replenishedMass;
        Info << "Total replenished mass in this run is: " << totRepMass_ << " [kg]" <<endl;
    }
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

BedCollapse::BedCollapse
(
    const dictionary& coeffs,
    const fvMesh& mesh,
    volScalarField& porosity,
    volScalarField& porosityArch,
    volScalarField& T,
    volScalarField& rho,
    PtrList<volScalarField>& Ym,
    PtrList<volScalarField>& Ys,
    autoPtr<volScalarField>& solidH,
    const volScalarField& cellVolume,
    SolidState& solidState
)
:
    mesh_(mesh),
    bedCollapseSwitch_(coeffs.lookupOrDefault("bedCollapse",false)),
    replenishSwitch_(coeffs.lookupOrDefault("replenish",false)),
    collapseMovesSolidMass_
    (
        coeffs.lookupOrDefault("collapseMovesSolidMass",false)
    ),
    porosity_(porosity),
    porosityArch_(porosityArch),
    T_(T),
    rho_(rho),
    Ym_(Ym),
    Ys_(Ys),
    solidH_(solidH),
    cellVolume_(cellVolume),
    solidState_(solidState),
    solidOccupiedCellLastStep_
    (
        IOobject
        (
            "solidOccupiedCellLastStep",
            mesh.time().timeName(),
            mesh,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh,
        dimensionedScalar("zero", dimless, 0.0)
    ),
    totRepMass_(0.),
    cumulativeCollapseMass_(0.0)
{}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

BedCollapse::~BedCollapse()
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

bool BedCollapse::collapseOnto(const List<Field<label>>& procFlipList)
{
    bool evaluate = false;
    forAll(procFlipList,listI)
    {
        if (procFlipList[listI].size() > 0)
        {
            evaluate = true;
        }
    }
    if (!evaluate)
    {
        return false;
    }

    solidState_.correctSolidPresentBoundaryConditions();
    porosity_.correctBoundaryConditions();
    solidOccupiedCellLastStep_ = solidOccupiedCellLastStep_*0;

    List<Field<scalar>> globalIndex(Pstream::nProcs());
    const volScalarField globalIndices(globalCellNumbering(globalIndex));

    const scalarField takeFrom(upperSolidNeighbour(globalIndices));

    const List<List<label>> realRoutes
    (
        collapseRoutes(procFlipList, globalIndex, takeFrom)
    );

    syncCollapseBoundaries();

    scalar replenishedMass = 0;
    scalar collapsedMass = 0;
    forAll(realRoutes,routeI)
    {
        walkCollapseRoute
        (
            realRoutes[routeI],
            globalIndex,
            globalIndices,
            replenishedMass,
            collapsedMass
        );
    }

    reportCollapsedMass(replenishedMass, collapsedMass);

    return true;
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// ************************************************************************* //
