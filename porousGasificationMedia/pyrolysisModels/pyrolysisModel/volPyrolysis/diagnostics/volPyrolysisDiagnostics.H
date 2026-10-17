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
    Foam::heterogeneousPyrolysisModels::VolPyrolysisDiagnostics

Description
    Diagnostic accounting for volPyrolysis.

    Holds the cumulative mass/heat counters and the solid mass budget
    terms, and formats the lines that report them. Pure bookkeeping: it
    keeps no references to fields, mesh or chemistry, and every value it
    receives is already computed (and already reduced, where the caller
    reduces) by volPyrolysis.

    Owned directly by volPyrolysis (like SolidStateChecker and
    SolidFluxLimiter) - no runtime selection, no dictionary entries.

    The chemistry time step and the replenished-mass total are not held
    here: the former steers the solver through maxTime(), the latter is
    part of the bed-collapse algorithm. Both are passed in for reporting.

SourceFiles
    volPyrolysisDiagnostics.C

\*---------------------------------------------------------------------------*/

#ifndef volPyrolysisDiagnostics_H
#define volPyrolysisDiagnostics_H

#include "dimensionedScalar.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

/*---------------------------------------------------------------------------*\
                  Class VolPyrolysisDiagnostics Declaration
\*---------------------------------------------------------------------------*/

class VolPyrolysisDiagnostics
{
    // Private data

        //- Cumulative lost mass of the condensed phase [kg]
        dimensionedScalar lostSolidMass_;

        //- Cumulative mass generation of the gas phase [kg]
        dimensionedScalar addedGasMass_;

        //- Total mass gas flux at the pyrolysing walls [kg/s]
        dimensionedScalar totalGasMassFlux_;

        //- Total heat release rate [J/s]
        dimensionedScalar totalHeatRR_;

        //- Total Ym mass integral at construction, for the conservation
        //  diagnostic [kg]. On restart this holds the restart inventory,
        //  so conservation is checked from the restart onward.
        scalar initialTotalYmMass_;

        //- Cumulative solid mass advected out through boundaries [kg]
        //  Credited in the conservation diagnostic so the budget stays
        //  checkable in domains with an open outlet.
        scalar cumulativeYmOutflow_;

        //- Cumulative mass created by the max(0) clip on Ym [kg]
        //  Reported with the mass budget, so the budget cannot look closed
        //  while the clip is quietly paying for it.
        scalar cumulativeYmClip_;

        //- Cumulative solid mass destroyed by emptying flipped cells [kg]
        //  Nonzero only under emptyFlippedCells; reported with the mass
        //  budget so it isn't hidden in a conserved-looking total.
        scalar cumulativeFlipMass_;

        //- Cumulative solid mass shed at the foot of a bed-motion route [kg]
        //  Nonzero only under collapseMovesSolidMass. A route spanning
        //  processors can also create mass not charged here - see
        //  volPyrolysis::recoverPorosity().
        scalar cumulativeCollapseMass_;


    // Private member functions

        //- Disallow default bitwise copy construct
        VolPyrolysisDiagnostics(const VolPyrolysisDiagnostics&);

        //- Disallow default bitwise assignment
        void operator=(const VolPyrolysisDiagnostics&);


public:

    // Constructors

        //- Construct with every counter at zero
        VolPyrolysisDiagnostics();


    //- Destructor
    ~VolPyrolysisDiagnostics();


    // Member Functions

        // Solid mass budget

            //- Add one specie's initial inventory [kg]
            void addInitialYmMass(const scalar mass);

            //- Add solid mass advected out through boundaries [kg]
            void addYmOutflow(const scalar mass);

            //- Add mass fabricated by the max(0) clip on Ym [kg]
            void addYmClip(const scalar mass);

            //- Add mass destroyed by emptying flipped cells [kg], reduced
            void addFlipMass(const scalar mass);

            //- Add mass shed at the foot of bed-motion routes [kg], reduced
            void addCollapseMass(const scalar mass);


        // Chemistry statistics

            //- Set the current gas release rate [kg/s]
            void setTotalGasMassFlux(const dimensionedScalar& rate);

            //- Set the current heat release rate [J/s]
            void setTotalHeatReleaseRate(const dimensionedScalar& rate);

            //- Add gas mass produced over a step [kg]
            void addGasMass(const dimensionedScalar& mass);

            //- Add solid mass lost over a step [kg]
            void addLostSolidMass(const dimensionedScalar& mass);


        // Reporting

            //- Print the solid mass budget line
            void reportSolidMassBudget(const scalar currentYmMass) const;

            //- Print the pyrolysis summary (volPyrolysis::info())
            void reportPyrolysis
            (
                const scalar replenishedMass,
                const scalar chemistryTime
            ) const;
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
