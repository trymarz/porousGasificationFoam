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
    Template implementation of \c constSolidRad.
    See the class Description in the .H.

    The constructor reads \c kappa [1/m], \c sigmaS [1/m] and
    \c emissivity [-] from the component's coefficient sub-dictionary,
    defaulting to 0 if absent. The \c Ostream operator serialises the
    inherited thermo state followed by the three radiation constants.

\*---------------------------------------------------------------------------*/

#include "constSolidRad.H"
#include "IOstreams.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

template<class thermo>
constSolidRad<thermo>::constSolidRad(const dictionary& dict)
:
    thermo(dict),
    kappa_(dict.lookupOrDefault("kappa",0.)),
    sigmaS_(dict.lookupOrDefault("sigmaS",0.)),
    emissivity_(dict.lookupOrDefault("emissivity",0.))
{}


// * * * * * * * * * * * * * * * Ostream Operator  * * * * * * * * * * * * * //

template<class thermo>
Ostream& operator<<(Ostream& os, const constSolidRad<thermo>& pg)
{
    os << static_cast<const thermo&>(pg);
    os << tab << pg.kappa_ << tab << pg.sigmaS_ << tab << pg.emissivity_;

    os.check("Ostream& operator<<(Ostream& os, const constSolidRad& st)");
    return os;
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// ************************************************************************* //
