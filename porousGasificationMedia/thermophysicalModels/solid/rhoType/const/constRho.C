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

#include "constRho.H"

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::constRho::constRho(const dictionary& dict)
:
    rho_(readScalar(dict.subDict("density").lookup("rho"))),
    rhoApparent_(0)
{
    // Required, no fallback: a silent default would hide a wrong particle
    // volume. density.name() carries the file and the <specie>Coeffs path.
    const dictionary& density = dict.subDict("density");

    if (!density.found("rhoApparent"))
    {
        FatalIOErrorInFunction(density)
            << "Missing required entry 'rhoApparent' [kg/m^3], the apparent "
            << "(envelope) density, next to the skeletal 'rho'."
            << exit(FatalIOError);
    }

    rhoApparent_ = density.get<scalar>("rhoApparent");

    if (rhoApparent_ <= 0)
    {
        FatalIOErrorInFunction(density)
            << "'rhoApparent' must be positive, got " << rhoApparent_
            << exit(FatalIOError);
    }
}


// * * * * * * * * * * * * * * * Ostream Operator  * * * * * * * * * * * * * //

Foam::Ostream& Foam::operator<<(Ostream& os, const constRho& cr)
{
    os << cr.rho_;

    os.check("Ostream& operator<<(Ostream& os, const constRho& cr)");
    return os;
}


// ************************************************************************* //
