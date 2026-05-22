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
    Foam::linearSolidTransport

Description
    Solid transport package with linear-in-T thermal conductivity.

    \f[
        K(T) = K_0 + n_0\, T
    \f]

    Two parameters \c K0_ (offset) and \c n0_ (slope) read from the
    component's \c transport sub-dict. A cheap stand-in for materials
    where K varies roughly linearly over the temperature range of
    interest, e.g. some metal alloys near room T.

    NOTE: the original file Description claimed "Exponential" — that
    was a copy-paste error retained in earlier versions. The class
    name and formula above are the correct ones.

SourceFiles
    linearSolidTransport.H
    linearSolidTransport.C
    linearSolidTransportI.H

\*---------------------------------------------------------------------------*/

#ifndef linearSolidTransport_H
#define linearSolidTransport_H

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

// Forward declaration of friend functions and operators
template<class thermo> class linearSolidTransport;
template<class thermo>
Ostream& operator<<
(
    Ostream&,
    const linearSolidTransport<thermo>&
);


/*---------------------------------------------------------------------------*\
                        Class linearSolidTransport Declaration
\*---------------------------------------------------------------------------*/

template<class thermo>
class linearSolidTransport
:
    public thermo
{
    // Private data

        //- Constant thermal coefficient.
        scalar K0_;

        //- Exponent coefficient
        scalar n0_;

    // Private Member Functions

        //- Construct from components
        inline linearSolidTransport
        (
            const thermo& t,
            const scalar K0,
            const scalar n0
        );


public:

    // Constructors

        //- Construct as named copy
        inline linearSolidTransport
        (
            const word&,
            const linearSolidTransport&
        );

        //- Construct from dictionary
        linearSolidTransport(const dictionary&);

    // Member functions

        //- Return the instantiated type name
        static word typeName()
        {
            return "linear<" + thermo::typeName() + '>';
        }

        //- Thermal conductivity [W/mK]
        inline scalar K(const scalar T) const;

        //- Thermal diffusivity [kg/ms]
        inline scalar alpha(const scalar T) const;


    // Ostream Operator

        friend Ostream& operator<< <thermo>
        (
            Ostream&,
            const linearSolidTransport&
        );
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#include "linearSolidTransportI.H"

#ifdef NoRepository
#   include "linearSolidTransport.C"
#endif

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
