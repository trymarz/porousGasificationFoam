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
    Foam::solidArrheniusReactionRate

Description
    Standard Arrhenius reaction-rate law for solids.

    \f[
        k_f(T) = A \, \exp(-T_a / T) \quad \text{for } T > T_{crit},
        \quad 0 \text{ otherwise}
    \f]

    Three scalars per reaction read from chemistryProperties:
      - \c A    — pre-exponential factor (units depend on reaction order),
      - \c Ta   — activation temperature = E_a / R [K],
      - \c Tcrit — minimum temperature for the reaction to fire [K].

    The Tcrit gate is a hard switch — below Tcrit the reaction does not
    contribute. See README → Input File Reference for the reaction-row
    format.

SourceFiles
    solidArrheniusReactionRate.H
    solidArrheniusReactionRateI.H

\*---------------------------------------------------------------------------*/

#ifndef solidArrheniusReactionRate_H
#define solidArrheniusReactionRate_H

#include "scalarField.H"
#include "typeInfo.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
// Forward declaration of friend functions and operators
class solidArrheniusReactionRate;
Ostream& operator<<(Ostream&, const solidArrheniusReactionRate&);

/*---------------------------------------------------------------------------*\
                       Class solidArrheniusReactionRate Declaration
\*---------------------------------------------------------------------------*/

class solidArrheniusReactionRate
{
    // Private data

        scalar A_;
        scalar Ta_;
        scalar Tcrit_;


public:

    // Constructors

        //- Construct from components
        inline solidArrheniusReactionRate
        (
            const scalar A,
            const scalar Ta,
            const scalar Tcrit
        );

        //- Construct from Istream
        inline solidArrheniusReactionRate
        (
            const speciesTable& species,
            Istream& is
        );


    // Member Functions

        //- Return the type name
        static word type()
        {
            return "SolidArrhenius";
        }

        inline scalar operator()
        (
            const scalar T,
            const scalar p,
            const scalarField& c
        ) const;


    // Ostream Operator

        inline friend Ostream& operator<<
        (
            Ostream&,
            const solidArrheniusReactionRate&
        );
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#include "solidArrheniusReactionRateI.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
