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

\*---------------------------------------------------------------------------*/

#include "volPyrolysisDiagnostics.H"

#include "dimensionSets.H"
#include "IOstreams.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

VolPyrolysisDiagnostics::VolPyrolysisDiagnostics()
:
    lostSolidMass_(dimensionedScalar("zero", dimMass, 0.0)),
    addedGasMass_(dimensionedScalar("zero", dimMass, 0.0)),
    totalGasMassFlux_(dimensionedScalar("zero", dimMass/dimTime, 0.0)),
    totalHeatRR_(dimensionedScalar("zero", dimEnergy/dimTime, 0.0)),
    initialTotalYmMass_(0.0),
    cumulativeYmOutflow_(0.0),
    cumulativeYmClip_(0.0),
    cumulativeFlipMass_(0.0),
    cumulativeCollapseMass_(0.0)
{}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

VolPyrolysisDiagnostics::~VolPyrolysisDiagnostics()
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

void VolPyrolysisDiagnostics::addInitialYmMass(const scalar mass)
{
    initialTotalYmMass_ += mass;
}


void VolPyrolysisDiagnostics::addYmOutflow(const scalar mass)
{
    cumulativeYmOutflow_ += mass;
}


void VolPyrolysisDiagnostics::addYmClip(const scalar mass)
{
    cumulativeYmClip_ += mass;
}


void VolPyrolysisDiagnostics::addFlipMass(const scalar mass)
{
    cumulativeFlipMass_ += mass;
}


void VolPyrolysisDiagnostics::addCollapseMass(const scalar mass)
{
    cumulativeCollapseMass_ += mass;
}


void VolPyrolysisDiagnostics::setTotalGasMassFlux
(
    const dimensionedScalar& rate
)
{
    totalGasMassFlux_ = rate;
}


void VolPyrolysisDiagnostics::setTotalHeatReleaseRate
(
    const dimensionedScalar& rate
)
{
    totalHeatRR_ = rate;
}


void VolPyrolysisDiagnostics::addGasMass(const dimensionedScalar& mass)
{
    addedGasMass_ += mass;
}


void VolPyrolysisDiagnostics::addLostSolidMass(const dimensionedScalar& mass)
{
    lostSolidMass_ += mass;
}


void VolPyrolysisDiagnostics::reportSolidMassBudget
(
    const scalar currentYmMass
) const
{
    Info<< "solid mass budget: sum(Ym_i * V) = " << currentYmMass
        << ", initial = " << initialTotalYmMass_
        << ", fabricated by clip = " << cumulativeYmClip_
        << ", destroyed by flip = " << cumulativeFlipMass_
        << ", destroyed by collapse = " << cumulativeCollapseMass_
        << endl;
}


void VolPyrolysisDiagnostics::reportPyrolysis
(
    const scalar replenishedMass,
    const scalar chemistryTime
) const
{
    // Text kept byte-identical to the former volPyrolysis::info(),
    // spelling included: log-based regressions grep for it.
    Info<< "\nPyrolysis: " << endl;

    Info<< indent << "Total gas mass produced  [kg] = " << addedGasMass_.value() << nl
        << indent << "Total solid mass lost    [kg] = " << lostSolidMass_.value() << nl
        << indent << "Total mass replenished   [kg] = " << replenishedMass << nl
        << indent << "Realese rate of pyrolysis gases  [kg/s] = " << totalGasMassFlux_.value() << nl
        << indent << "Heat release rate [J/s] = " << totalHeatRR_.value() << nl;

        if (chemistryTime < GREAT )
        {
            Info << indent << "Suggested chemical time step from heterogeneous reactions [s] = "
                 << chemistryTime << nl;
        }
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// ************************************************************************* //
