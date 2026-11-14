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
    (0 empty, 1 trace, 2 dispersed, 3 connected). solidConnectedAtStepStart,
    the source gate, is registered but not written.

    One classification per step, classify(), on the post-step state: the
    mass the species step left, the rho the energy step corrected, and the
    porosity they give. It runs after Ts is recovered and before porosity
    is written (#173), and sets solidRegime and the gate together. Both Ts
    rules read it. Only when the bed motion rewrites cells is it redone, on
    what the motion left; the rules are not re-applied.

    The gate is the step-start state for the next step's sources: the
    radiation, the reacting flags and the exchange are all fixed before
    that step's species step, and the solid energy equation reads the same
    gate, so every reaction heat and exchanged joule is booked on both sides.

    refreshSolidPresent() keeps solidPresent on the porosity as it stands,
    for the flip search and the checks around the bed motion.

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

    Field access (state update):
      reads   porosity, and the Ym and rho callers pass in;
      writes  only what it owns: solidPresent, solidRegime and
              connectedAtStepStart; deriveYiFromYm() writes the Ys passed.

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

        //- Cells a step's reaction emptied: present at its start, empty after
        //  They take the trace rule, so their Ts stays defined.
        boolList emptiedThisStep_;


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

        //- The gate's patch values, from the porosity and solidPresent patches
        void setGatePatches();


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

            //- Porosity 1 - totalYm/rho, rho floored by SMALL [-]
            //  The one expression for it: classify() and the porosity
            //  recovery must agree on it bit for bit.
            static tmp<volScalarField> porosityFromMass
            (
                const volScalarField& totalYm,
                const volScalarField& rho
            );


        // Solid temperature, one rule per regime

            //- Ts with the dispersed rule applied: Tgas where classify() found
            //  no connected solid, if dispersedSolidTemperature is gas
            tmp<volScalarField> withDispersedRule
            (
                const volScalarField& Ts,
                const volScalarField& Tgas
            ) const;

            //- Ts with the trace rule applied: Tgas where classify() found trace
            //  solidH/rhoCp is scale-invariant in the mass, so a trace cell
            //  would echo its donor's Ts at full strength.
            tmp<volScalarField> withTraceRule
            (
                const volScalarField& Ts,
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

            //- Each cell's regime into solidRegime, and the gate from it
            //  The step's one classification; the gate's patches follow in
            //  correctBoundaryConditions().
            void classify
            (
                const volScalarField& porosity,
                const volScalarField& totalYm,
                const volScalarField& rho
            );

            //- Refresh solidPresent's patch values
            void correctSolidPresentBoundaryConditions();

            //- Refresh solidPresent's and the gate's patch values
            void correctBoundaryConditions();
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
