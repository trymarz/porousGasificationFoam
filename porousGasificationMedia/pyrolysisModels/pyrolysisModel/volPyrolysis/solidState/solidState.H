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
    Foam::heterogeneousPyrolysisModels::SolidState

Description
    The solid regimes of a cell, and the thresholds that separate them.

    With alpha = 1 - porosity the solid volume fraction, a cell is

    - occupied  where alpha > 0 exactly (porosity < 1);
    - present   where sum_i Ym_i > solidStateTolerance*rho, i.e. alpha
                above the presence floor; under it a cell holds only trace;
    - source-admissible where it is occupied and porosity < criticalPorosity:
                the binary gate on the volumetric solid sources;
    - packed    at porosity = minPorosity, the floor SolidFluxLimiter
                fills to and the porosity guard holds.

    The thresholds are read from the owning model's coefficients, under
    the keys they always had. The class classifies only: flipping, bed
    motion, flux limiting and the fail-fast guards ask it and stay where
    they are.

    The masks are refreshed by two snapshot calls, at the points of the
    step the solver has always refreshed them at:

    - snapshotAfterTransport(): occupancy, from the recovered porosity;
    - snapshotStepStart(): the source gate the next step's explicit
      sources read. Taken at the end of the step, so it lags whatever
      writes porosity before the next one (#127 leaves that to step 2).

    The presence floor is not a snapshot: it is evaluated on the mass the
    caller passes in, at each Ts recovery.

    Owned directly by volPyrolysis (like SolidStateChecker and
    SolidFluxLimiter) - no runtime selection.

SourceFiles
    solidState.C

\*---------------------------------------------------------------------------*/

#ifndef solidState_H
#define solidState_H

#include "fvMesh.H"
#include "volFields.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

/*---------------------------------------------------------------------------*\
                       Class SolidState Declaration
\*---------------------------------------------------------------------------*/

class SolidState
{
    // Private data

        //- Mesh the solid fields live on
        const fvMesh& mesh_;

        //- Porosity the regimes are derived from [-]
        const volScalarField& porosity_;

        //- Presence floor, relative to a packed cell's mass [-]
        //  Also the fail-fast guards' relative slack, under the same key.
        scalar solidStateTolerance_;

        //- Porosity below which the solid sources are admitted [-]
        scalar criticalPorosity_;

        //- Lowest porosity a cell may be packed to [-]
        scalar minPorosity_;

        //- 1 where the cell holds any solid (porosity < 1), else 0
        volScalarField occupied_;

        //- Complement of occupied_
        volScalarField absent_;

        //- 1 where the volumetric solid sources are admitted, else 0
        //  Stored rather than recomputed so the radiation model can hold a
        //  reference to it.
        volScalarField sourceTermsAdmissible_;


    // Private member functions

        //- Disallow default bitwise copy construct
        SolidState(const SolidState&);

        //- Disallow default bitwise assignment
        void operator=(const SolidState&);

        //- Abort unless each threshold lies where its regime needs it
        void checkThresholds(const dictionary& coeffs) const;


public:

    // Constructors

        //- Construct from the owning model's coefficients and porosity
        //  Masks start empty; classifyInitialState() fills them.
        SolidState
        (
            const dictionary& coeffs,
            const volScalarField& porosity
        );


    //- Destructor
    ~SolidState();


    // Member Functions

        // Thresholds

            //- Presence floor and guard slack [-]
            scalar solidStateTolerance() const
            {
                return solidStateTolerance_;
            }

            //- Porosity below which the solid sources are admitted [-]
            scalar criticalPorosity() const
            {
                return criticalPorosity_;
            }

            //- Lowest porosity a cell may be packed to [-]
            scalar minPorosity() const
            {
                return minPorosity_;
            }


        // Masks

            //- 1 where the cell holds any solid, else 0
            const volScalarField& occupied() const
            {
                return occupied_;
            }

            //- 1 where the cell holds no solid, else 0
            const volScalarField& absent() const
            {
                return absent_;
            }

            //- 1 where the volumetric solid sources are admitted, else 0
            const volScalarField& sourceTermsAdmissible() const
            {
                return sourceTermsAdmissible_;
            }

            //- Solid volume fraction alpha = 1 - porosity [-]
            tmp<volScalarField> solidFraction() const;

            //- Positive only where the cell holds solid above the floor
            //  A fraction solidStateTolerance of what a packed cell would
            //  hold, floored by SMALL too, since rho is zero where empty.
            tmp<volScalarField> massAbovePresenceFloor
            (
                const volScalarField& totalYm,
                const volScalarField& rho
            ) const;

            //- 1 in an occupied cell holding only trace, else 0
            tmp<volScalarField> belowPresenceFloor
            (
                const volScalarField& totalYm,
                const volScalarField& rho
            ) const;


        // Solid temperature

            //- Ts: TsFromEnthalpy where solid is present, Tgas where trace
            //  solidH/rhoCp is scale-invariant in the mass, so a trace cell
            //  would echo its donor's Ts at full strength.
            tmp<volScalarField> recoverTs
            (
                const volScalarField& TsFromEnthalpy,
                const volScalarField& totalYm,
                const volScalarField& rho,
                const volScalarField& Tgas
            ) const;


        // Snapshots

            //- Classify every cell, patches included, from the initial porosity
            void classifyInitialState();

            //- Occupancy from the porosity just recovered, internal cells only
            void snapshotAfterTransport();

            //- Freeze the source gate the next step's explicit sources read
            void snapshotStepStart();

            //- Refresh the occupancy mask's patch values
            void correctOccupiedBoundaryConditions();

            //- Refresh the absence mask's patch values
            void correctAbsentBoundaryConditions();
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
