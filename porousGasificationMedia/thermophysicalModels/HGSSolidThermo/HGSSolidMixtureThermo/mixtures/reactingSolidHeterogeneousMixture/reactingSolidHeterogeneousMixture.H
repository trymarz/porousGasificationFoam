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
    Foam::reactingSolidHeterogeneousMixture

Description
    Multi-component solid mixture with heterogeneous reactions of the
    form \c S1 + G1 = S2 + G2 (gas species on both sides).

    The canonical mixture used by PGF for gasification, combustion and
    char oxidation cases. Inherits both the multi-component solid
    composition (Ys / rho / K / Cp from
    \c multiComponentSolidMixture) and the PtrList of heterogeneous
    reactions (each entry is a \c solidHeterogeneousReaction). The
    reactions are read from the \c solidReactions list in
    \c chemistryProperties; each entry has the form

    \code
    irreversibleSolidArrheniusHeterogeneousReaction
    n_S1 S1 + n_G1 G1  =  n_S2 S2 + n_G2 G2
    (A Ta Tcrit heatOfReaction n1 n2 ...)
    \endcode

    Bound to the global gas-phase species list (\c gasPhaseGases) at
    construction so the chemistry can later map per-reaction gas
    indices to the solver's gas-species fields.

SourceFiles
    reactingSolidHeterogeneousMixture.H
    reactingSolidHeterogeneousMixture.C

\*---------------------------------------------------------------------------*/

#ifndef reactingSolidHeterogeneousMixture_H
#define reactingSolidHeterogeneousMixture_H

#include "multiComponentSolidMixture.H"
#include "solidHeterogeneousReaction.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

/*---------------------------------------------------------------------------*\
                    Class reactingSolidHeterogeneousMixture Declaration
\*---------------------------------------------------------------------------*/

template<class ThermoSolidType>
class reactingSolidHeterogeneousMixture
:
    public multiComponentSolidMixture<ThermoSolidType>,
    public PtrList<solidHeterogeneousReaction>
{
    // Private Member Functions

        //- Disallow default bitwise copy construct
        reactingSolidHeterogeneousMixture(const reactingSolidHeterogeneousMixture&);

        //- Disallow default bitwise assignment
        void operator=(const reactingSolidHeterogeneousMixture&);


public:

    //- The type of thermo package this mixture is instantiated for
    typedef ThermoSolidType thermoType;


    // Constructors

        //- Construct from dictionary and mesh
        reactingSolidHeterogeneousMixture(const dictionary&, const fvMesh&, const PtrList<volScalarField>& gasPhaseGases);


    //- Destructor
    virtual ~reactingSolidHeterogeneousMixture()
    {}


    // Member functions

        //- Read dictionary
        void read(const dictionary&);
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#ifdef NoRepository
#   include "reactingSolidHeterogeneousMixture.C"
#endif

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
