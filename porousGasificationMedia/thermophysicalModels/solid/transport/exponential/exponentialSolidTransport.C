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
    Template implementation of \c exponentialSolidTransport.
    See the class Description in the .H.

    The constructor reads \c K0 [W/m/K], \c n0 [-] and \c Tref [K]
    from the \c transport sub-dictionary. Together these define
    \f$K(T) = K_0 (T/T_{ref})^{n_0}\f$. The \c Ostream operator
    serialises the inherited thermo state followed by the three
    transport coefficients.

\*---------------------------------------------------------------------------*/

#include "exponentialSolidTransport.H"
#include "IOstreams.H"

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

template<class thermo>
Foam::exponentialSolidTransport<thermo>::exponentialSolidTransport
(
    const dictionary& dict
)
:
    thermo(dict),
    K0_(0.0),
    n0_(0.0),
    Tref_(0.0)
{
    const dictionary& subDict = dict.subDict("transport");
    K0_ = readScalar(subDict.lookup("K0"));
    n0_ = readScalar(subDict.lookup("n0"));
    Tref_ = readScalar(subDict.lookup("Tref"));
}


// * * * * * * * * * * * * * * * IOstream Operators  * * * * * * * * * * * * //

template<class thermo>
Foam::Ostream& Foam::operator<<
(
    Ostream& os, const exponentialSolidTransport<thermo>& et
)
{
    operator<<(os, static_cast<const thermo&>(et));
    os << tab << et.K0_  << tab << et.n0_ << tab << et.Tref_;

    os.check
    (
        "Ostream& operator<<(Ostream& os, const exponentialSolidTransport& et)"
    );

    return os;
}


// ************************************************************************* //
