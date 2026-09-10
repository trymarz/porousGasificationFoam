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
    Two-sided saturation limit on the solid volumetric face flux.

    Every solid field - the specie mass concentrations and the solid enthalpy
    - rides one shared face flux, so bounding that flux once, before any of
    them is transported, bounds all of them at the same time and by the same
    amount: a face moves the same solid in every equation that sees it.

    The limit has two ends. A face may not carry out more of a specie than
    its donor cell holds (solidDonorLimit), which makes Ym_i >= 0 and
    porosity <= 1 structural rather than clipped; and it may not deliver more
    solid volume than its receiver has room for (solidReceiverLimit), so
    porosity >= minPorosity_ and the flux into a filling cell falls to zero -
    this is the repulsion that makes a bed jam instead of overpacking.

    Each face takes the tighter of its two ends' factors, one factor per face,
    so the scaling neither creates nor destroys mass. The sweep is repeated
    nSolidFluxLimiterCorrectors_ times in Zalesak/FCT form, each sweep
    re-reading the budgets the previous one left, so a jam can travel back up
    the bed within one time step instead of one cell per step.

    Owned by volPyrolysis by plain composition, in the manner of STmodel_ and
    HTmodel_: only one limiter algorithm exists, so there is no runtime
    selection table. The limited flux is returned rather than written, since
    the field it scales is consumed by the transport stages that own it.

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

        //- Solid-phase velocity the unlimited flux is asked for by
        const volVectorField& Us_;

        //- Solid specie mass concentrations [kg/m3]
        //  Non-const: the budgets refresh the Ym_ patches before reading
        //  them, since a fixedYm value is one stage stale until corrected.
        PtrList<volScalarField>& Ym_;

        //- Solid skeletal density [kg/m3]
        const volScalarField& rho_;

        //- Solid temperature [K], for the chemistry porosity source
        const volScalarField& T_;

        //- Solid chemistry, for RRs(i) and RRpor(T)
        const porousThermoSolidChemistryModel<HGSSolidThermo>& chemistry_;

        //- The owning model's active flag, read fresh each call
        const Switch& active_;

        //- Switch for Eulerian advection of the solid fields by Us
        //  When false the solid volumetric flux is held at zero, so the
        //  solid specie masses and the solid enthalpy are only updated by
        //  chemistry - and so, through the mass, is the recovered porosity.
        Switch advectSolidFields_;

        //- Sweeps of the solid flux limiter [-]
        //  Each sweep re-reads the budgets the previous one left, so a jam
        //  can travel back up the bed within one time step instead of one
        //  cell per step. Zero leaves the flux Us asks for untouched.
        label nSolidFluxLimiterCorrectors_;

        //- Lowest porosity the solid flux limiter will pack a cell to [-]
        //  Zero admits a cell of pure solid. A case with a packing limit of
        //  its own raises it, and the flux into a cell then stops at that
        //  fraction instead.
        scalar minPorosity_;

        //- Relative slack admitted when judging the swept residual [-]
        //  Read from the same pyrolysisCoeffs key volPyrolysis reads it
        //  from, and silently: the owning model already logs the value.
        scalar solidStateTolerance_;


    // Private member functions

        //- Disallow default bitwise copy construct
        SolidFluxLimiter(const SolidFluxLimiter&);

        //- Disallow default bitwise assignment
        void operator=(const SolidFluxLimiter&);

        //- Solid volumetric face flux [m3/s] asked for by Us
        //  Zero unless advectSolidFields_ is set, and unlimited: this is
        //  what Us asks for. limit() scales it into the flux the transport
        //  stages ride.
        tmp<surfaceScalarField> solidVolFlux() const;

        //- Face flux leaving and entering each cell, summed per cell
        //  Both sums positive, direction taken from the scaled flux itself
        //  rather than from Us. Local: every face of a cell is visible to
        //  the rank owning it, a coupled patch face included.
        void accumulateFaceFlux
        (
            const surfaceScalarField& phi,
            const surfaceScalarField& lambda,
            scalarField& sumOut,
            scalarField& sumIn
        ) const;

        //- Assemble the budgets the flux limit is built from
        //  Per specie, the transport and chemistry terms of the equation
        //  being limited (phiYm, RRsolid) and their sum (phiYmTotal); then
        //  the solid volume that mass flux carries (phiSolidVol), the solid
        //  volume fraction each cell holds (alphaS) and the rate chemistry
        //  changes it at (RRpor). Refreshes the Ym_ patches first: a
        //  fixedYm value is read from the cell behind it and is one stage
        //  stale until corrected.
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

        //- Share of the outgoing flux each cell can supply, per cell
        //  A face may not carry out more of a specie than its donor holds,
        //  which makes Ym_i >= 0 and porosity <= 1 structural rather than
        //  clipped. Exact, per specie, from the transport scheme's own face
        //  fluxes. With credit set, what arrives over the step may leave
        //  again within it.
        void solidDonorLimit
        (
            const PtrList<surfaceScalarField>& phiYm,
            const PtrList<volScalarField>& RRsolid,
            const surfaceScalarField& lambda,
            const bool credit,
            scalarField& lambdaDonor
        ) const;

        //- Share of the incoming flux each cell has room for, per cell
        //  A face may not deliver more solid volume than fits in its
        //  receiver, so porosity >= minPorosity_ and the flux into a
        //  filling cell falls to zero - this is the repulsion. With credit
        //  set, what leaves over the step counts as room within it.
        void solidReceiverLimit
        (
            const surfaceScalarField& phiSolidVol,
            const volScalarField& alphaS,
            const volScalarField& RRpor,
            const surfaceScalarField& lambda,
            const bool credit,
            scalarField& lambdaReceiver
        ) const;

        //- Give each face the tighter of its two ends' factors
        //  One factor per face, so both sides move the same solid and the
        //  scaling neither creates nor destroys mass: what cannot move
        //  stays in the donor and the bed jams. lambda slices allLambda,
        //  which is the list the coupled-face sync operates on.
        void applySolidFaceLimit
        (
            const surfaceScalarField& phiYmTotal,
            const scalarField& lambdaDonor,
            const scalarField& lambdaReceiver,
            scalarField& allLambda,
            surfaceScalarField& lambda
        ) const;

        //- Report the limiter's activity and what the sweeps left over
        //  A limiter that acts is Us asking the bed for something it cannot
        //  do, so it reports when it acts and is quiet when it does not.
        //  The residual is read off the state the limited flux will
        //  produce, since the sweep count is capped and convergence is not
        //  guaranteed.
        void reportSolidFluxLimiter
        (
            const PtrList<surfaceScalarField>& phiYm,
            const PtrList<volScalarField>& RRsolid,
            const surfaceScalarField& phiSolidVol,
            const volScalarField& alphaS,
            const volScalarField& RRpor,
            const surfaceScalarField& lambda
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

        //- The solid volumetric face flux the transport stages ride [m3/s]
        //  The flux Us asks for, scaled to what the cells can supply and
        //  take. The fail-fast guards in the owning model are the backstop
        //  for what the sweeps do not reach.
        tmp<surfaceScalarField> limit();
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
