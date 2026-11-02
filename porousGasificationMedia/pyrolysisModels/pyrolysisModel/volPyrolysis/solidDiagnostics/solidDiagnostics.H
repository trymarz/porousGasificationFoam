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
    Foam::heterogeneousPyrolysisModels::SolidDiagnostics

Description
    Fail-fast guards and per-step reports on the solid state.

    The solid transport equations are explicit and unbounded, so an
    out-of-range value is diagnosed at the equation that produced it,
    not clipped and left to fail downstream with no trace of its origin.

    Each stage scans its field for the first cell outside the bounds
    that field's meaning admits, and aborts there. Bounds live here, not
    at each call site, so a stage just asks for the check it needs.

    A scan returns -1 when failOnInvalidSolidState_ is off - that's the
    one kill switch, no call site tests it itself. Scans are rank-local:
    an invalid cell on any rank aborts the whole job.

    Thresholds and masks come from SolidState, which the switch cannot
    reach: turning the guards off leaves every physics mask as it is.
    Also owns the porosity/solid-mass consistency check and the per-step
    solid reports: mass budget, derived mass fractions, temperature range.

    Solid boundary policy (#162): a patch either declares the incoming solid
    (every solid Yi and Ts fixedValue) or inherits it from the face cell (no
    Yi fixed). checkBoundaryDeclarations() rejects anything in between at
    start-up. checkSolidCrossing() acts on solid entering an inheriting patch,
    which would copy the face cell's mass and enthalpy in, and on solid
    entering declaring faces whose Yi sum to zero, which would bring enthalpy
    without mass, and on solid leaving through a wall-type patch, whose Us
    should close it. A face counts once the volume crossing it over the step
    exceeds the check slack of the face cell's volume.

    Owned directly by volPyrolysis (like SolidFluxLimiter) - no runtime
    selection, since only one checking policy exists.

SourceFiles
    solidDiagnostics.C

\*---------------------------------------------------------------------------*/

#ifndef solidDiagnostics_H
#define solidDiagnostics_H

#include "fvMesh.H"
#include "volFields.H"
#include "surfaceFields.H"
#include "PtrList.H"
#include "Switch.H"
#include "Time.H"
#include "solidState.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

/*---------------------------------------------------------------------------*\
                    Class SolidDiagnostics Declaration
\*---------------------------------------------------------------------------*/

class SolidDiagnostics
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

        //- Solid regimes: the guards' slack and the porosity floor
        //  The slack is sized for round-off on explicit updates only;
        //  anything past it is a real overshoot.
        const SolidState& solidState_;

        //- Bound on a physically credible solid temperature [K]
        //  Ts = solidH/(totalYm*Cp) is meaningless once totalYm is
        //  near zero.
        scalar maxSolidTemperature_;

        //- Action on solid crossing a patch it should not:
        //  error, warn or ignore
        word solidBoundaryPolicy_;

        //- Patches that inherit the solid state from the face cell
        labelList inheritingPatches_;

        //- Patches that declare the incoming solid (every Yi and Ts fixed)
        labelList declaringPatches_;

        //- Wall-type patches, which solid must not leave through
        labelList wallPatches_;

        //- Patches already reported for inflow, indexed by patch
        boolList inflowReported_;

        //- Patches already reported for outflow, indexed by patch
        boolList outflowReported_;


    // Private member functions

        //- Disallow default bitwise copy construct
        SolidDiagnostics(const SolidDiagnostics&);

        //- Disallow default bitwise assignment
        void operator=(const SolidDiagnostics&);

        //- First cell that's non-finite or outside [lower, upper], else -1
        //  Skips cells where the optional mask isn't positive. Rank-local.
        label firstInvalidCell
        (
            const volScalarField& fld,
            const scalar lower,
            const scalar upper,
            const scalarField* mask = nullptr
        ) const;

        //- Names of the given patches, for the log
        wordList patchNames(const labelList& patches) const;

        //- Whether a face carrying volume flux phi into (or out of) celli
        //  exceeds the check slack of the cell's volume over this step
        bool beyondSlack(const scalar phi, const label celli) const;

        //- sum_i Ym_i on patchi's faces [kg/m3]
        scalarField patchTotalYm
        (
            const PtrList<volScalarField>& Ym,
            const label patchi
        ) const;

        //- Error or warn per solidBoundaryPolicy
        void applyBoundaryPolicy(const string& what, const string& remedy);


public:

    // Constructors

        //- Construct from the owning model's coefficients, solid state,
        //  mesh and time
        SolidDiagnostics
        (
            const dictionary& coeffs,
            const SolidState& solidState,
            const fvMesh& mesh,
            const Time& time
        );


    //- Destructor
    ~SolidDiagnostics();


    // Member Functions

        //- First cell with an impossible recovered porosity, else -1
        //  Bounds [1-maxSolidFraction-tol, 1+tol]: >1 is negative solid mass,
        //  below the floor means no gas volume left for the gas solve.
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

        //- Reports the worst cell violating 1 - porosity = sum_i Ym_i/rho_i
        //  Catches two in-range fields disagreeing with each other, which
        //  a plain min/max can't. Always prints; warns past tolerance.
        void checkConsistency
        (
            const volScalarField& porosity,
            const PtrList<volScalarField>& Ym,
            const volScalarField& rho
        ) const;

        //- Info: transported solid mass against the booked losses
        void reportMassBudget
        (
            const PtrList<volScalarField>& Ym,
            const scalar initialMass,
            const scalar clipped,
            const scalar flipped,
            const scalar collapsed
        ) const;

        //- Info: range of each derived solid mass fraction
        void reportDerivedSpecies(const PtrList<volScalarField>& Ys) const;

        //- Print min/max Ts over the cells holding solid
        void reportTemperatureRange(const volScalarField& T) const;

        //- Classifies each patch as declaring or inheriting the solid state
        //  Aborts on a partial composition, or a fixed Yi without a fixed
        //  Ts; warns when the fixed Yi do not sum to 1.
        void checkBoundaryDeclarations
        (
            const PtrList<volScalarField>& Ys,
            const volScalarField& Ts
        );

        //- Applies solidBoundaryPolicy to solid entering a patch that does
        //  not declare it, or leaving through a wall. Reports each once.
        void checkSolidCrossing
        (
            const surfaceScalarField& phiSolid,
            const PtrList<volScalarField>& Ys,
            const PtrList<volScalarField>& Ym
        );
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
