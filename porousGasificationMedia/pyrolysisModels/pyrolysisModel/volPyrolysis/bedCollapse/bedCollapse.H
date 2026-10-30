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

Class
    Foam::heterogeneousPyrolysisModels::BedCollapse

Description
    Bed collapse: the solid above each cell volPyrolysis empties falls
    into it.

    From each emptied cell (a route's foot) the route climbs +z, cell by
    cell, while the cell above still holds solid; the last solid cell is
    the route's top. The master builds every route from the globally
    numbered cells, then each rank walks the part of each route it holds,
    pulling the solid of the cell above into the cell below.

    Under collapseMovesSolidMass the mass fields (Ym, solid enthalpy)
    move with the porosity and the foot's mass leaves the domain, charged
    to collapsedMass(). Unset, only porosity, T and rho move. Under
    replenish the top keeps its solid (fresh solid enters from above);
    otherwise it is emptied. A route spanning processors can create mass
    that collapsedMass() does not charge - see walkCollapseRoute().

    Reads bedCollapse, replenish and collapseMovesSolidMass from the
    owning model's coefficients. Owned directly by volPyrolysis (like
    SolidFluxLimiter) - no runtime selection, since only one bed-motion
    model exists.

SourceFiles
    bedCollapse.C

\*---------------------------------------------------------------------------*/

#ifndef bedCollapse_H
#define bedCollapse_H

#include "fvMesh.H"
#include "volFields.H"
#include "PtrList.H"
#include "Switch.H"
#include "solidState.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

/*---------------------------------------------------------------------------*\
                        Class BedCollapse Declaration
\*---------------------------------------------------------------------------*/

class BedCollapse
{
    // Private data

        //- Mesh the solid fields live on
        const fvMesh& mesh_;

        //- Switch for bed motion model
        Switch bedCollapseSwitch_;

        //- Switch for bed replenishment
        Switch replenishSwitch_;

        //- Switch for bed motion that moves the solid rather than the porosity
        //  Set, bed motion moves Ym_/solidH_ down the route by the same
        //  cell-volume ratio it applies to porosity; unset, only porosity moves.
        Switch collapseMovesSolidMass_;

        //- Porosity of the owning model
        volScalarField& porosity_;

        volScalarField& porosityArch_;

        //- Solid temperature
        volScalarField& T_;

        //- Solid density [kg/m3]
        volScalarField& rho_;

        //- Solid specie mass concentrations [kg/m3]
        PtrList<volScalarField>& Ym_;

        //- Solid mass fractions; only their neighbour values are refreshed
        PtrList<volScalarField>& Ys_;

        //- Solid enthalpy; bound by owner, since volPyrolysis creates it
        //  after this object
        autoPtr<volScalarField>& solidH_;

        const volScalarField& cellVolume_;

        //- Solid regimes; supplies the solidPresent mask
        SolidState& solidState_;

        //- Bed-motion scratch, zeroed before each use; not a solid mask
        volScalarField solidOccupiedCellLastStep_;

        //- Total replenished mass
        scalar totRepMass_;

        //- Cumulative solid mass shed at the foot of a bed-motion route [kg]
        //  Nonzero only under collapseMovesSolidMass_. A route spanning
        //  processors can also create mass not charged here - see walkCollapseRoute().
        scalar cumulativeCollapseMass_;


    // Private member functions

        //- Disallow default bitwise copy construct
        BedCollapse(const BedCollapse&);

        //- Disallow default bitwise assignment
        void operator=(const BedCollapse&);

        //- The solid one cell hands to the cell below in a bed collapse
        struct MovedSolid
        {
            scalar porosity;
            scalar porosityArch;
            scalar T;
            scalar rho;
            scalarList Ym;
            scalar H;
        };

        //- A global index per cell; fills globalIndex per rank
        tmp<volScalarField> globalCellNumbering
        (
            List<Field<scalar>>& globalIndex
        ) const;

        //- Global index of the solid cell above each solid cell, or -1
        scalarField upperSolidNeighbour
        (
            const volScalarField& globalIndices
        ) const;

        //- The chains of cells, foot to top, that each collapse moves
        List<List<label>> collapseRoutes
        (
            const List<Field<label>>& procFlipList,
            const List<Field<scalar>>& globalIndex,
            const scalarField& takeFrom
        ) const;

        //- Neighbour values a route reads across processor boundaries
        void syncCollapseBoundaries();

        //- Move the solid one cell down along this rank's part of a route
        void walkCollapseRoute
        (
            const List<label>& route,
            const List<Field<scalar>>& globalIndex,
            const volScalarField& globalIndices,
            scalar& replenishedMass,
            scalar& collapsedMass
        );

        //- The solid of a cell on this rank
        MovedSolid localSolid(const label cellI) const;

        //- The solid of the cell across a processor face
        MovedSolid neighbourSolid
        (
            const label patchID,
            const label faceID
        ) const;

        //- Write the solid from the cell above into cellI
        void moveSolidInto
        (
            const label cellI,
            const MovedSolid& from,
            const scalar cellVolumeRatio
        );

        //- moveSolidInto() from the cell fromGlobalI on another rank
        void moveSolidFromNeighbourProcessor
        (
            const label cellI,
            const label fromGlobalI,
            const volScalarField& globalIndices
        );

        //- Add the mass a route's foot sheds to collapsedMass
        void chargeRouteFoot
        (
            const label footCell,
            scalar& collapsedMass
        ) const;

        //- Empty a route's top unless replenishing; add its mass
        void emptyRouteTop(const label topCell, scalar& replenishedMass);

        //- Reduce and record the mass a bed collapse moved
        void reportCollapsedMass
        (
            scalar replenishedMass,
            scalar collapsedMass
        );


public:

    // Constructors

        //- Construct from the owning model's coefficients, mesh and the
        //  solid fields it moves
        BedCollapse
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
        );


    //- Destructor
    ~BedCollapse();


    // Member Functions

        //- The bedCollapse switch
        const Switch& active() const
        {
            return bedCollapseSwitch_;
        }

        //- The replenish switch
        const Switch& replenish() const
        {
            return replenishSwitch_;
        }

        //- The collapseMovesSolidMass switch
        const Switch& movesSolidMass() const
        {
            return collapseMovesSolidMass_;
        }

        //- Total replenished mass [kg]; accumulated on the master only
        scalar replenishedMass() const
        {
            return totRepMass_;
        }

        //- Cumulative solid mass shed at route feet [kg]
        scalar collapsedMass() const
        {
            return cumulativeCollapseMass_;
        }

        //- The solid above each emptied cell moves down; false, and
        //  nothing touched, when no rank has a cell to empty
        bool collapseOnto(const List<Field<label>>& procFlipList);
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
