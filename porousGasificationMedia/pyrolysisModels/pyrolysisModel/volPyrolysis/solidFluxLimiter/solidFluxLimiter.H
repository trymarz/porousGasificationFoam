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

    Every solid field rides one shared face flux, so limiting it once bounds
    all of them by the same amount at every face: a donor cannot be asked for
    more than it holds, and a receiver not given more than it has room for.
    See solidDonorLimit() and solidReceiverLimit() for the two bounds, and
    limit() for how the Zalesak/FCT sweeps combine them.

    Owned by volPyrolysis by plain composition, in the manner of STmodel_ and
    HTmodel_: only one limiter algorithm exists, so there is no runtime
    selection table.

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
        //  False holds the solid volumetric flux at zero, so the specie
        //  masses/enthalpy (and porosity, through them) update by chemistry only.
        Switch advectSolidFields_;

        //- Sweeps of the solid flux limiter [-]
        //  Each sweep re-reads the previous one's budgets (see limit()).
        //  Zero leaves the flux Us asks for untouched.
        label nSolidFluxLimiterCorrectors_;

        //- Lowest porosity the solid flux limiter will pack a cell to [-]
        //  Zero admits pure solid; a case with its own packing limit raises
        //  it, and inflow then stops at that fraction.
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

        //- Solid volumetric face flux [m3/s] asked for by Us, unlimited
        //  limit() scales this into the flux the transport stages ride.
        tmp<surfaceScalarField> solidVolFlux() const;

        //- Face flux leaving and entering each cell, summed per cell
        //  Both sums positive; direction from the scaled flux, not Us. Local:
        //  every face is visible to the rank owning it, coupled included.
        void accumulateFaceFlux
        (
            const surfaceScalarField& phi,
            const surfaceScalarField& lambda,
            scalarField& sumOut,
            scalarField& sumIn
        ) const;

        //- Assemble the budgets the flux limit is built from
        //  Per-specie transport/chemistry terms and their sum, the solid
        //  volume they carry, alphaS and RRpor. Refreshes Ym_'s patches first.
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
        //  making Ym_i >= 0 structural. With credit, what arrives may leave.
        void solidDonorLimit
        (
            const PtrList<surfaceScalarField>& phiYm,
            const PtrList<volScalarField>& RRsolid,
            const surfaceScalarField& lambda,
            const bool credit,
            scalarField& lambdaDonor
        ) const;

        //- Share of the incoming flux each cell has room for, per cell
        //  A face may not deliver more volume than its receiver has room for
        //  (porosity >= minPorosity_); the repulsion. Credit adds outflow room.
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
        //  One factor per face conserves mass: what cannot move stays in the
        //  donor. lambda slices allLambda, the list the coupled sync uses.
        void applySolidFaceLimit
        (
            const surfaceScalarField& phiYmTotal,
            const scalarField& lambdaDonor,
            const scalarField& lambdaReceiver,
            scalarField& allLambda,
            surfaceScalarField& lambda
        ) const;

        //- Report the limiter's activity and what the sweeps left over
        //  Quiet unless it acted. The residual is read off the state the
        //  limited flux will produce, since convergence is not guaranteed.
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
        //  Us's flux, scaled to what cells can supply/take. The fail-fast
        //  guards in the owning model back up what the sweeps miss.
        tmp<surfaceScalarField> limit();
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
