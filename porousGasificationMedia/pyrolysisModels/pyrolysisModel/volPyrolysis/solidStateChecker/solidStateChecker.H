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

    The solid transport equations are explicit and unbounded, so an
    out-of-range value is diagnosed at the equation that produced it,
    not clipped and left to fail downstream with no trace of its origin.

    Each stage scans its field for the first cell outside the bounds
    that field's meaning admits, and aborts there. Bounds live here, not
    at each call site, so a stage just asks for the check it needs.

    A scan returns -1 when failOnInvalidSolidState_ is off - that's the
    one kill switch, no call site tests it itself. Scans are rank-local:
    an invalid cell on any rank aborts the whole job.

    Also owns the solid-presence mask the Ts guards use, and the
    porosity/solid-mass consistency check.

    Owned directly by volPyrolysis (like SolidFluxLimiter) - no runtime
    selection, since only one checking policy exists.

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

        //- Master switch for the fail-fast guards
        //  When on, an impossible porosity, Ym, solid enthalpy or
        //  temperature aborts at the equation that produced it.
        Switch failOnInvalidSolidState_;

        //- Relative slack allowed by the guards [-]
        //  Sized for round-off on explicit updates only; anything past
        //  this is a real overshoot.
        scalar solidStateTolerance_;

        //- Bound on a physically credible solid temperature [K]
        //  Ts = solidH/(totalYm*Cp) is meaningless once totalYm is
        //  near zero.
        scalar maxSolidTemperature_;


    // Private member functions

        //- Disallow default bitwise copy construct
        SolidStateChecker(const SolidStateChecker&);

        //- Disallow default bitwise assignment
        void operator=(const SolidStateChecker&);

        //- First cell that's non-finite or outside [lower, upper], else -1
        //  Skips cells where the optional mask isn't positive. Rank-local.
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

        //- First cell with an impossible recovered porosity, else -1
        //  Bounds [-tol, 1+tol]: >1 means negative solid mass, <0 means
        //  packed past pure solid.
        label firstInvalidPorosity(const volScalarField& por) const;

        //- First cell with an impossible Ym or solid enthalpy, else -1
        //  Extensive fields, so the allowed undershoot scales with the
        //  amount present.
        label firstInvalidExtensive(const volScalarField& fld) const;

        //- First cell with an impossible solid temperature, else -1
        //  Checked only where the mask is positive.
        label firstInvalidTemperature
        (
            const volScalarField& T,
            const scalarField& mask
        ) const;

        //- First cell with an impossible recovered solid temperature,
        //  else -1. Unmasked; lower bound is 0, since no solid means
        //  no solid enthalpy either.
        label firstInvalidTemperature(const volScalarField& T) const;

        //- Aborts on an impossible solid state, reporting where and
        //  whatever context the caller provides. Never returns.
        void abort
        (
            const word& stage,
            const volScalarField& fld,
            const label cellI,
            const string& context
        ) const;

        //- Positive only where the cell holds solid; the Ts guards' mask
        //  A solid temperature is meaningless without solid to carry it -
        //  testing exact zero would flag trace dust as a defect.
        tmp<volScalarField> solidPresent
        (
            const volScalarField& totalYm,
            const volScalarField& rho
        ) const;

        //- Reports the worst cell violating 1 - porosity = sum_i Ym_i/rho_i
        //  Catches two in-range fields disagreeing with each other, which
        //  a plain min/max can't. Always prints; warns past tolerance.
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
