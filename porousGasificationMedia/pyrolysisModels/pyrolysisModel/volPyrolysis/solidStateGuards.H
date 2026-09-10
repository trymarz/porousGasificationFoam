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

Description
    Fail-fast guards on the solid state.

    The solid transport equations are explicit and unbounded, so a value
    outside its physical range is a defect to diagnose at the equation that
    produced it, not a number to clip and carry forward into a downstream
    exception that says nothing about where it came from.

    Free functions rather than members: every stage of volPyrolysis that
    updates a solid field checks it the same way, and the check needs nothing
    from the model beyond the mesh and the time it reports.

SourceFiles
    solidStateGuards.H

\*---------------------------------------------------------------------------*/

#ifndef solidStateGuards_H
#define solidStateGuards_H

#include "volFields.H"
#include "fvMesh.H"
#include "Time.H"

#include <cmath>

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

//- Index of the first cell whose value is not finite or lies outside
//  [lower, upper]; -1 when the field is valid everywhere.
//  Cells where the optional mask is not positive are skipped - a solid
//  temperature is not a defect in a cell holding no solid.
//  Local: an invalid state on any rank aborts the whole job.
inline label firstInvalidSolidCell
(
    const volScalarField& fld,
    const scalar lower,
    const scalar upper,
    const scalarField* mask = nullptr
)
{
    forAll(fld, cellI)
    {
        if (mask && (*mask)[cellI] <= 0.0)
        {
            continue;
        }

        const scalar value = fld[cellI];

        if (!std::isfinite(value) || value < lower || value > upper)
        {
            return cellI;
        }
    }

    return -1;
}


//- Abort on an impossible solid state, reporting where it is and whatever
//  the calling stage knows is relevant.
inline void reportInvalidSolidState
(
    const fvMesh& mesh,
    const Time& time,
    const word& stage,
    const volScalarField& fld,
    const label cellI,
    const string& context
)
{
    FatalErrorInFunction
        << "Impossible solid state produced by " << stage << nl << nl
        << "    time         = " << time.timeName() << nl
        << "    deltaT       = " << time.deltaTValue() << nl
        << "    field        = " << fld.name() << nl
        << "    value        = " << fld[cellI] << nl
        << "    processor    = " << Pstream::myProcNo() << nl
        << "    cell         = " << cellI << nl
        << "    cell centre  = " << mesh.C()[cellI] << nl
        << "    cell volume  = " << mesh.V()[cellI] << nl
        << context.c_str() << nl
        << "The solid transport equations are explicit and unbounded. Either"
        << " the solid velocity Us is invalid, or the explicit update has"
        << " overshot at this time step." << nl
        << exit(FatalError);
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
