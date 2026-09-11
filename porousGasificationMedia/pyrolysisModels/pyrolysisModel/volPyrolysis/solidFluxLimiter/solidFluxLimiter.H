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
    Foam::heterogeneousPyrolysisModels::SolidFluxLimiter

Description
    Scales the shared solid face flux so it never asks a cell for more
    mass than it holds (donor limit) or more volume than it has room for
    (receiver limit). See solidDonorLimit(), solidReceiverLimit() and
    limit() for the two bounds and how the sweeps combine them.

    Owned directly by volPyrolysis (like STmodel_, HTmodel_) - no runtime
    selection, since only one limiter algorithm exists.

SourceFiles
    solidFluxLimiter.C

\*---------------------------------------------------------------------------*/

#ifndef solidFluxLimiter_H
#define solidFluxLimiter_H

#include "fvMesh.H"
#include "volFields.H"
#include "surfaceFields.H"
#include "PtrList.H"
#include "Switch.H"
#include "HGSSolidThermo.H"
#include "porousThermoSolidChemistryModel.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

/*---------------------------------------------------------------------------*\
                     Class SolidFluxLimiter Declaration
\*---------------------------------------------------------------------------*/

class SolidFluxLimiter
{
    // Private data

        //- Mesh the solid fields live on
        const fvMesh& mesh_;

        //- Run time, for deltaT and the field name stamp
        const Time& time_;

        //- Solid velocity; source of the unlimited flux
        const volVectorField& Us_;

        //- Solid specie mass concentrations [kg/m3]
        //  Non-const: patches are refreshed before reading, since a
        //  fixedYm value is one stage stale otherwise.
        PtrList<volScalarField>& Ym_;

        //- Solid skeletal density [kg/m3]
        const volScalarField& rho_;

        //- Solid temperature [K], for the chemistry porosity source
        const volScalarField& T_;

        //- Solid chemistry, for RRs(i) and RRpor(T)
        const porousThermoSolidChemistryModel<HGSSolidThermo>& chemistry_;

        //- Owning model's active flag, re-read each call
        const Switch& active_;

        //- Switch for advecting the solid fields by Us
        //  False zeroes the solid flux, so Ym/enthalpy/porosity change
        //  by chemistry only.
        Switch advectSolidFields_;

        //- Number of limiter sweeps [-]
        //  Each sweep re-reads the last one's budgets (see limit()).
        //  Zero disables limiting.
        label nSolidFluxLimiterCorrectors_;

        //- Lowest porosity a cell may be packed to [-]
        //  Zero allows pure solid; raise it to stop inflow at that
        //  fraction.
        scalar minPorosity_;

        //- Relative slack allowed when judging the sweep residual [-]
        //  Shares its dictionary key with volPyrolysis, which already
        //  logs the value.
        scalar solidStateTolerance_;


    // Private member functions

        //- Disallow default bitwise copy construct
        SolidFluxLimiter(const SolidFluxLimiter&);

        //- Disallow default bitwise assignment
        void operator=(const SolidFluxLimiter&);

        //- Unlimited solid volumetric face flux [m3/s], from Us
        //  limit() scales this down into the flux transport actually uses.
        tmp<surfaceScalarField> solidVolFlux() const;

        //- Sums the (scaled) flux leaving/entering each cell
        //  Both sums positive. Rank-local; includes coupled faces.
        void accumulateFaceFlux
        (
            const surfaceScalarField& phi,
            const surfaceScalarField& fluxScale,
            scalarField& sumOut,
            scalarField& sumIn
        ) const;

        //- Builds the per-specie and total flux/chemistry budgets used
        //  below (alphaS, RRpor included). Refreshes Ym_'s patches first.
        void solidFluxBudgets
        (
            const surfaceScalarField& phiSolid,
            PtrList<surfaceScalarField>& phiYm,
            PtrList<volScalarField>& RRsolid,
            tmp<surfaceScalarField>& tPhiYmTotal,
            tmp<surfaceScalarField>& tPhiSolidVol,
            tmp<volScalarField>& tAlphaS,
            tmp<volScalarField>& tRRpor
        );

        //- Per-cell cap on outgoing flux: can't give away more of a
        //  specie than it holds, keeping Ym_i >= 0. With credit, flux
        //  arriving this sweep may leave again the same sweep.
        void solidDonorLimit
        (
            const PtrList<surfaceScalarField>& phiYm,
            const PtrList<volScalarField>& RRsolid,
            const surfaceScalarField& fluxScale,
            const bool credit,
            scalarField& donorScale
        ) const;

        //- Per-cell cap on incoming flux: can't receive more solid
        //  volume than its porosity has room for (>= minPorosity_).
        //  Credit adds the cell's own outflow back as extra room.
        void solidReceiverLimit
        (
            const surfaceScalarField& phiSolidVol,
            const volScalarField& alphaS,
            const volScalarField& RRpor,
            const surfaceScalarField& fluxScale,
            const bool credit,
            scalarField& receiverScale
        ) const;

        //- Sets each face's scale factor to the tighter of its two ends
        //  Keeps mass conserved: what can't move stays with the donor.
        //  fluxScale slices allFluxScale, which the coupled-patch sync uses.
        void applySolidFaceLimit
        (
            const surfaceScalarField& phiYmTotal,
            const scalarField& donorScale,
            const scalarField& receiverScale,
            scalarField& allFluxScale,
            surfaceScalarField& fluxScale
        ) const;

        //- Logs what the sweeps limited and what residual they left
        //  Silent unless something was limited; sweeps aren't guaranteed
        //  to converge, so the residual is measured, not assumed zero.
        void reportSolidFluxLimiter
        (
            const PtrList<surfaceScalarField>& phiYm,
            const PtrList<volScalarField>& RRsolid,
            const surfaceScalarField& phiSolidVol,
            const volScalarField& alphaS,
            const volScalarField& RRpor,
            const surfaceScalarField& fluxScale
        ) const;


public:

    // Constructors

        //- Construct from the owning model's coefficients and fields
        SolidFluxLimiter
        (
            const dictionary& coeffs,
            const fvMesh& mesh,
            const volVectorField& Us,
            PtrList<volScalarField>& Ym,
            const volScalarField& rho,
            const volScalarField& T,
            const porousThermoSolidChemistryModel<HGSSolidThermo>& chemistry,
            const Switch& active
        );


    //- Destructor
    ~SolidFluxLimiter();


    // Member Functions

        //- Solid volumetric face flux used by transport [m3/s]
        //  Us's flux, scaled to what each cell can supply/receive. The
        //  owning model's fail-fast guards catch whatever the sweeps miss.
        tmp<surfaceScalarField> limit();
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
