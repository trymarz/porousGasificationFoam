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

Typedefs
    Foam::solidThermoPhysicsTypes

Description
    Type aliases for the per-component solid thermo packages.

    Three concrete combinations are exposed:
      - \c constSolidThermoPhysics    — constTransport + constRad +
                                         constThermo + constRho
        (the typical entry — type name in dictionary:
         const<constRad<constThermo<constRho>>>);
      - \c expoSolidThermoPhysics     — constTransport + constRad +
                                         exponentialThermo + constRho
        (constant K, T-dependent Cp);
      - \c linearSolidThermoPhysics   — linearTransport + constRad +
                                         constThermo + constRho
        (linear K(T), constant Cp).

    The matching \c solidThermoType strings used in
    \c chemistryProperties are produced by the nested \c typeName()
    of each wrapping template (see the README → Input File Reference
    → solidThermophysicalProperties for the user-facing form).

\*---------------------------------------------------------------------------*/

#ifndef solidThermoPhysicsTypes_H
#define solidThermoPhysicsTypes_H

#include "constRho.H"

#include "constSolidThermo.H"
#include "exponentialSolidThermo.H"

#include "constSolidTransport.H"
#include "exponentialSolidTransport.H"
#include "linearSolidTransport.H"

#include "constSolidRad.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
    typedef constSolidTransport
        <
            constSolidRad
            <
                constSolidThermo
                <
                    constRho
                >
            >
        >
        constSolidThermoPhysics;

    typedef constSolidTransport
        <
            constSolidRad
            <
                exponentialSolidThermo
                <
                    constRho
                >
            >
        >
        expoSolidThermoPhysics;

    typedef linearSolidTransport
        <
            constSolidRad
            <
                constSolidThermo
                <
                    constRho
                >
            >
        >
        linearSolidThermoPhysics;
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
