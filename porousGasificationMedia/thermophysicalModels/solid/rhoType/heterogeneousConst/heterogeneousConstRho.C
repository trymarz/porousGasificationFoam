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
    Implementation of \c heterogeneousConstRho.
    See the class Description in the .H.

    Structurally identical to \c constRho but used for the
    heterogeneous (bed-scale) solid components where a distinct class
    is needed for the run-time type selection in
    \c solidThermoPhysicsTypes. Reads \c rho from the \c density
    sub-dictionary.

\*---------------------------------------------------------------------------*/

#include "heterogeneousConstRho.H"

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::heterogeneousConstRho::heterogeneousConstRho(const dictionary& dict)
:
    rho_(readScalar(dict.subDict("density").lookup("rho")))
{}


// * * * * * * * * * * * * * * * Ostream Operator  * * * * * * * * * * * * * //

Foam::Ostream& Foam::operator<<(Ostream& os, const heterogeneousConstRho& cr)
{
    os << cr.rho_;

    os.check("Ostream& operator<<(Ostream& os, const constRho& cr)");
    return os;
}


// ************************************************************************* //
