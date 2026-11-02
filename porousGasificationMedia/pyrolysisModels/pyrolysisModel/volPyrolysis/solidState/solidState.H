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
    What a cell holds, as one of four solid regimes, and the thresholds
    between them.

    The regimes lie on one axis, the solid volume fraction
    alpha = 1 - porosity = sum_i Ym_i/rho. Each threshold belongs to the
    regime below it:

        empty      alpha = 0
        trace      alpha <= dispersedAboveSolidFraction
                   too little solid to trust its own temperature: Ts = Tgas
        dispersed  alpha <= connectedAboveSolidFraction
                   the solid no longer forms a connected matrix: no solid
                   sources, and the cell may be emptied
        connected  alpha <= maxSolidFraction
                   the full model

    alpha never exceeds maxSolidFraction: SolidFluxLimiter stops inflow
    there and SolidDiagnostics aborts past it. The regimes hold at any
    scale - a cell may be a piece of one pellet or hold many pellets.

    The class owns what depends on the regime alone, and the two floors
    that keep heat capacities finite at the ends of the axis: the solid's
    in an empty cell, the gas's in a fully solid one. Callers combine the
    regime with what else they know (motion, fluxes) to decide, e.g.,
    which cells to empty.

    Written fields: solidPresent (1 where alpha > 0) and solidRegime
    (0 empty, 1 trace, 2 dispersed, 3 connected), both as at the step's
    start. solidConnectedAtStepStart, the source gate, is registered but
    not written.

    Two snapshots, at the points of the step the solver has always taken
    them at:

    - refreshSolidPresent(): solidPresent, from the recovered porosity;
    - freezeConnectedAtStepStart(): the source gate the next step's
      explicit sources read. Taken at the end of the step, so it lags
      whatever writes porosity before the next one (#127 leaves that to
      step 2).

    The trace test is not a snapshot: it is evaluated on the mass the
    caller passes in.

    Read from the owning model's coefficients:

        solidRegimes
        {
            dispersedAboveSolidFraction  1e-8;
            connectedAboveSolidFraction  1e-4;
            maxSolidFraction             0.9999;
            dispersedSolidTemperature    solid;   // solid | gas
        }
        solidStateCheckTolerance         1e-8;

    The values shown are the defaults. The keys these replace
    (criticalPorosity, minPorosity, solidStateTolerance,
    gasTemperatureBelowCriticalPorosity) are refused with the exact
    replacement, since two of them were porosities.

    Owned directly by volPyrolysis (like SolidDiagnostics and
    SolidFluxLimiter) - no runtime selection. The radiation models read
    it too, through the inline accessors only: their library builds first.

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
public:

    //- The solid regimes, in order of rising solid fraction
    enum class Regime : label
    {
        empty = 0,
        trace = 1,
        dispersed = 2,
        connected = 3
    };


private:

    // Private data

        //- Mesh the solid fields live on
        const fvMesh& mesh_;

        //- Porosity the regimes are derived from [-]
        const volScalarField& porosity_;

        //- Solid fraction at or below which a cell holds only trace [-]
        scalar dispersedAboveSolidFraction_;

        //- Solid-fraction slack the fail-fast checks and reports allow [-]
        //  Changes no result, only whether a run warns or aborts.
        scalar checkTolerance_;

        //- Solid fraction above which the solid is connected [-]
        scalar connectedAboveSolidFraction_;

        //- Solid fraction no cell may exceed [-]
        scalar maxSolidFraction_;

        //- 1 - dispersedAboveSolidFraction_, for tests on the porosity [-]
        scalar traceAbovePorosity_;

        //- 1 - connectedAboveSolidFraction_, for tests on the porosity [-]
        scalar connectedBelowPorosity_;

        //- 1 - maxSolidFraction_, for tests on the porosity [-]
        scalar minPorosity_;

        //- Whether dispersed solid takes the gas temperature
        //  Energy the solid loses this way is not charged anywhere.
        bool dispersedTakesGasTemperature_;

        //- 1 where the cell holds any solid (porosity < 1), else 0
        volScalarField solidPresent_;

        //- Each cell's Regime, as a number, for output only
        volScalarField solidRegime_;

        //- 1 where the solid was connected when the step started, else 0
        //  The gate on the volumetric solid sources. Stored rather than
        //  recomputed so the radiation model can hold a reference to it.
        volScalarField connectedAtStepStart_;


    // Private member functions

        //- Disallow default bitwise copy construct
        SolidState(const SolidState&);

        //- Disallow default bitwise assignment
        void operator=(const SolidState&);

        //- Abort, naming the replacement, if a key this class replaced is set
        void rejectReplacedKeys(const dictionary& coeffs) const;

        //- Whether dispersed solid takes the gas temperature, from its key
        static bool readDispersedSolidTemperature(const dictionary& regimes);

        //- Abort unless each threshold lies where its regime needs it
        void checkThresholds(const dictionary& regimes) const;

        //- Log the regimes and their thresholds, least solid first
        void printRegimes() const;


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

        // Thresholds, in solid fraction

            //- Solid fraction at or below which a cell holds only trace [-]
            scalar dispersedAboveSolidFraction() const
            {
                return dispersedAboveSolidFraction_;
            }

            //- Solid fraction above which the solid is connected [-]
            scalar connectedAboveSolidFraction() const
            {
                return connectedAboveSolidFraction_;
            }

            //- Solid fraction no cell may exceed [-]
            scalar maxSolidFraction() const
            {
                return maxSolidFraction_;
            }


        // Thresholds, in porosity, for tests on the porosity field

            //- Porosity above which a cell holds only trace, or no solid [-]
            scalar traceAbovePorosity() const
            {
                return traceAbovePorosity_;
            }

            //- Porosity below which the solid is connected [-]
            scalar connectedBelowPorosity() const
            {
                return connectedBelowPorosity_;
            }

            //- Lowest porosity a cell may be packed to [-]
            scalar minPorosity() const
            {
                return minPorosity_;
            }


        // Check slack

            //- Solid-fraction slack the fail-fast checks and reports allow [-]
            scalar checkTolerance() const
            {
                return checkTolerance_;
            }


        // Masks

            //- 1 where the cell holds any solid, else 0
            const volScalarField& solidPresent() const
            {
                return solidPresent_;
            }

            //- Each cell's Regime, as a number, for output only
            const volScalarField& solidRegime() const
            {
                return solidRegime_;
            }

            //- 1 where the solid was connected when the step started, else 0
            const volScalarField& connectedAtStepStart() const
            {
                return connectedAtStepStart_;
            }

            //- Solid volume fraction alpha = 1 - porosity [-]
            tmp<volScalarField> solidFraction() const;

            //- Whether the cell holds dispersed or trace solid
            //  Read on the porosity as it stands, not on a snapshot.
            bool isDispersedOrTrace(const label cellI) const
            {
                return
                    porosity_[cellI] > connectedBelowPorosity_
                 && porosity_[cellI] < 1.0;
            }

            //- Whether the cell holds only trace solid, or none
            //  Read on the porosity as it stands, not on a snapshot.
            bool isTraceOrEmpty(const label cellI) const
            {
                return
                    porosity_[cellI] > traceAbovePorosity_
                 || porosity_[cellI] >= 1.0;
            }

            //- sum_i Ym_i per cell [kg/m^3]
            static tmp<volScalarField> totalMass
            (
                const PtrList<volScalarField>& Ym
            );

            //- Ys_i = Ym_i/sum_j Ym_j; a cell with no mass gets the
            //  placeholder Ys_0 = 1
            static void deriveYiFromYm
            (
                const PtrList<volScalarField>& Ym,
                PtrList<volScalarField>& Ys
            );

            //- Positive only where the cell holds more than trace
            //  dispersedAboveSolidFraction of what a packed cell would
            //  hold, floored by SMALL too, since rho is zero where empty.
            tmp<volScalarField> massBeyondTrace
            (
                const volScalarField& totalYm,
                const volScalarField& rho
            ) const;

            //- 1 in a cell holding solid, but only trace, else 0
            tmp<volScalarField> isTrace
            (
                const volScalarField& totalYm,
                const volScalarField& rho
            ) const;


        // Solid temperature, one rule per regime

            //- Ts with the dispersed rule applied: Tgas where dispersed, if
            //  dispersedSolidTemperature is gas; otherwise Ts as given
            tmp<volScalarField> withDispersedRule
            (
                const volScalarField& Ts,
                const volScalarField& Tgas
            ) const;

            //- Ts with the trace rule applied: Tgas where the cell holds trace
            //  solidH/rhoCp is scale-invariant in the mass, so a trace cell
            //  would echo its donor's Ts at full strength.
            tmp<volScalarField> withTraceRule
            (
                const volScalarField& Ts,
                const volScalarField& totalYm,
                const volScalarField& rho,
                const volScalarField& Tgas
            ) const;


        // Heat-capacity floors

            //- rhoCp, floored at SMALL so Ts = H/rhoCp stays defined
            //  An empty cell holds no solid and so no heat capacity.
            tmp<volScalarField> withSolidHeatCapacityFloor
            (
                const volScalarField& rhoCp
            ) const;

            //- Gas volume fraction (the porosity), floored at 1e-4
            //  A fully solid cell holds no gas; the floor keeps the gas
            //  heat capacity in the subintegrated exchange finite.
            tmp<volScalarField> gasFractionWithFloor() const;


        // Snapshots

            //- Classify every cell, patches included, from the initial porosity
            void classifyInitialState();

            //- solidPresent from the porosity just recovered, internal cells only
            void refreshSolidPresent();

            //- Freeze the source gate the next step's explicit sources read
            void freezeConnectedAtStepStart();

            //- Record each cell's regime in solidRegime
            //  The trace test reads the mass passed in, as withTraceRule does.
            void classify
            (
                const volScalarField& totalYm,
                const volScalarField& rho
            );

            //- Refresh solidPresent's patch values
            void correctSolidPresentBoundaryConditions();
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
