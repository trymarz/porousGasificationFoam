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
    Foam::heterogeneousPyrolysisModels::SolidStateChecker

Description
    Fail-fast guards on the solid state.

    The solid transport equations are explicit and unbounded, so a value
    outside its physical range is a defect to diagnose at the equation that
    produced it, not a number to clip and carry forward into a downstream
    exception that says nothing about where it came from.

    Every stage that updates a solid field checks it the same way: scan for
    the first cell outside the bounds that field's meaning admits, and abort
    from there reporting whatever the stage knows is relevant. The bounds are
    named here rather than spelled at each call site, so a stage asks for the
    check its field needs and nothing else.

    A scan returns -1 when failOnInvalidSolidState_ is unset, which is how the
    guards are switched off - no call site carries a test of its own. Scans
    are rank-local: an invalid state on any rank aborts the whole job.

    Also owns the two pieces of state that were only ever guard state - the
    solid-presence mask the two Ts guards are judged on, and the porosity /
    solid-mass consistency invariant, which reports whether or not the
    fail-fast guards are on.

    Owned by volPyrolysis by plain composition, in the manner of
    SolidFluxLimiter: only one checking policy exists, so there is no runtime
    selection table.

SourceFiles
    solidStateChecker.C

\*---------------------------------------------------------------------------*/

#ifndef solidStateChecker_H
#define solidStateChecker_H

#include "fvMesh.H"
#include "volFields.H"
#include "PtrList.H"
#include "Switch.H"
#include "Time.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

/*---------------------------------------------------------------------------*\
                    Class SolidStateChecker Declaration
\*---------------------------------------------------------------------------*/

class SolidStateChecker
{
    // Private data

        //- Mesh the solid fields live on
        const fvMesh& mesh_;

        //- Run time, for the time and the deltaT an abort reports
        const Time& time_;

        //- Switch for the fail-fast guards on the solid state
        //  When set, an impossible porosity, Ym, solid enthalpy or solid
        //  temperature aborts at the equation that produced it.
        Switch failOnInvalidSolidState_;

        //- Relative slack admitted by the fail-fast guards [-]
        //  Sized to absorb round-off on the explicit updates only; an
        //  excursion larger than this is a genuine overshoot.
        scalar solidStateTolerance_;

        //- Bound on a physically credible solid temperature [K]
        //  Ts is a ratio, solidH/(totalYm*Cp), and says nothing credible
        //  about a cell whose solid inventory is vanishing.
        scalar maxSolidTemperature_;


    // Private member functions

        //- Disallow default bitwise copy construct
        SolidStateChecker(const SolidStateChecker&);

        //- Disallow default bitwise assignment
        void operator=(const SolidStateChecker&);

        //- Index of the first cell not finite or outside [lower, upper]
        //  -1 when the field is valid everywhere. Cells where the optional
        //  mask is not positive are skipped. Rank-local, as the guards are.
        label firstInvalidCell
        (
            const volScalarField& fld,
            const scalar lower,
            const scalar upper,
            const scalarField* mask = nullptr
        ) const;


public:

    // Constructors

        //- Construct from the owning model's coefficients, mesh and time
        SolidStateChecker
        (
            const dictionary& coeffs,
            const fvMesh& mesh,
            const Time& time
        );


    //- Destructor
    ~SolidStateChecker();


    // Member Functions

        //- First cell whose recovered porosity is impossible, else -1
        //  Bounds -tol to 1 + tol: por > 1 is negative solid mass, por < 0 a
        //  cell packed past solid by the transport.
        label firstInvalidPorosity(const volScalarField& por) const;

        //- First cell whose extensive solid field is impossible, else -1
        //  Ym and the solid enthalpy. Extensive, so the admissible
        //  undershoot scales with the amount present in the field.
        label firstInvalidExtensive(const volScalarField& fld) const;

        //- First cell whose solid temperature is impossible, else -1
        //  A temperature slack below zero, not a mass one, and judged only
        //  where the mask is positive.
        label firstInvalidTemperature
        (
            const volScalarField& T,
            const scalarField& mask
        ) const;

        //- First cell whose recovered solid temperature is impossible, else -1
        //  Unmasked, and zero is the lower bound: a cell holding no solid
        //  holds no solid enthalpy either.
        label firstInvalidTemperature(const volScalarField& T) const;

        //- Abort on an impossible solid state, reporting where it is and
        //  whatever the calling stage knows is relevant. Never returns.
        void abort
        (
            const word& stage,
            const volScalarField& fld,
            const label cellI,
            const string& context
        ) const;

        //- Positive only where the cell holds solid; the Ts guards' mask
        //  A solid temperature means nothing without solid to carry it, and
        //  the test is a mass scale or 1e-18 kg/m3 of dust reads as a defect.
        tmp<volScalarField> solidPresent
        (
            const volScalarField& totalYm,
            const volScalarField& rho
        ) const;

        //- Report the worst cell violating 1 - porosity = sum_i Ym_i/rho_i
        //  A field-wide min/max cannot see two fields disagreeing while
        //  neither is out of range. Always prints; warns past the tolerance.
        void checkConsistency
        (
            const volScalarField& porosity,
            const PtrList<volScalarField>& Ym,
            const volScalarField& rho,
            const volScalarField& whereIs
        ) const;
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
