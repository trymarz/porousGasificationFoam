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
    Foam::constSolidTransport

Description
    Solid transport package with constant thermal conductivity.

    \f$ K(T) = K_0 \f$ — a single scalar read from the component's
    \c transport sub-dict (e.g. \c woodCoeffs.transport.K). Wraps a
    thermodynamics package; the K(T) override is the only addition
    over \c thermo, which still supplies Cp, hf, hs.

    Use this for solids where the temperature dependence of K is
    negligible compared with the temperature range of interest. For
    T-dependent conductivity, see \c linearSolidTransport (K = a T + b)
    or \c exponentialSolidTransport (K = K_0 (T/Tref)^n).

SourceFiles
    constSolidTransport.H
    constSolidTransport.C
    constSolidTransportI.H

\*---------------------------------------------------------------------------*/

#ifndef constSolidTransport_H
#define constSolidTransport_H

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

template<class thermo> class constSolidTransport;

template<class thermo>
Ostream& operator<<(Ostream&, const constSolidTransport<thermo>&);


/*---------------------------------------------------------------------------*\
                         Class constSolidTransport Declaration
\*---------------------------------------------------------------------------*/

template<class thermo>
class constSolidTransport
:
    public thermo
{
    // Private data

        //- Constant thermal conductivity.
        scalar K_;


    // Private Member Functions

        //- Construct from components
        inline constSolidTransport
        (
            const thermo& t,
            const scalar k
        );


public:

    // Constructors

        //- Construct as named copy
        inline constSolidTransport(const word&, const constSolidTransport&);

        //- Construct from Istream
        constSolidTransport(const dictionary&);


    // Member functions

        //- Return the instantiated type name
        static word typeName()
        {
            return "const<" + thermo::typeName() + '>';
        }

        //- Thermal conductivity [W/mK]
        inline scalar K(const scalar T) const;

        //- Thermal diffusivity [kg/ms]
        inline scalar alpha(const scalar T) const;


    // Ostream Operator

        friend Ostream& operator<< <thermo>
        (
            Ostream&,
            const constSolidTransport&
        );
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#include "constSolidTransportI.H"

#ifdef NoRepository
#   include "constSolidTransport.C"
#endif

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
