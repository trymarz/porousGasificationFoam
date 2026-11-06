/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           |
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
    Foam::solidReactionStoichiometry

Description
    How one heterogeneous reaction's rate r (Eq. 5 of Zuk et al. 2022) is
    split among species (Eq. 6). Built once per reaction; it depends only on
    the reaction's coefficients and the gas molar masses, never on the cell.

    - coeffs(): signed kg of each species per kg of rate, indexed like the
      ODE state (solids, then nSolids + pyrolysis-gas index).
    - heat(): reaction enthalpy per kg of rate (Eq. 12), not split.
    - agent(g): kg of gas reactant g consumed per kg of rate. Its Eq. 10
      resistance is agent(g)/Omega_diff (#201), so at the diffusion limit the
      agent is consumed as fast as Eq. 9 delivers it. This departs from the
      literal Eq. 10, under which the consumption would be agent(g) times that.
    - consumed(): kg of reactants per kg of rate (the relative-rate report).

    Mass-based mode (stoichiometricReactions false): every coefficient over
    the solid reactants' sum, or over the gas reactants' sum if there is no
    solid reactant (#201). Stoichiometric mode: solids per kg of solid
    reactant, gases weighted by their molar mass so that the net gas release
    equals the solid mass lost (section 4.2.3 of the paper).

SourceFiles
    solidReactionStoichiometry.C

\*---------------------------------------------------------------------------*/

#ifndef solidReactionStoichiometry_H
#define solidReactionStoichiometry_H

#include "solidHeterogeneousReaction.H"
#include "scalarList.H"
#include "labelList.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

/*---------------------------------------------------------------------------*\
                 Class solidReactionStoichiometry Declaration
\*---------------------------------------------------------------------------*/

class solidReactionStoichiometry
{
    // Private data

        //- ODE-state index of each species the reaction touches
        labelList species_;

        //- Signed kg of that species per kg of rate
        scalarList coeffs_;

        //- kg of each gas reactant consumed per kg of rate, in glhs() order
        scalarList agent_;

        //- Reaction enthalpy per kg of rate
        scalar heat_;

        //- kg of reactants per kg of rate
        scalar consumed_;


    // Private Member Functions

        //- Add c to species i's coefficient
        void add(const label i, const scalar c);

        //- Species i is a reactant consumed at c per kg of rate
        void consume(const label i, const scalar c);

        //- Gas reactant g (ODE-state index i) consumed at c per kg of rate;
        //  c is also its Eq. 10 factor
        void consumeAgent(const label g, const label i, const scalar c);

        //- stoichiometricReactions false
        void setMassBased
        (
            const solidHeterogeneousReaction& R,
            const labelUList& gasDictionaryBack,
            const label nSolids
        );

        //- stoichiometricReactions true
        void setStoichiometric
        (
            const solidHeterogeneousReaction& R,
            const UList<scalar>& W,
            const labelUList& gasDictionary,
            const labelUList& gasDictionaryBack,
            const label nSolids
        );


public:

    // Constructors

        //- Construct from the reaction. W is the gas molar masses in
        //  gas-phase order; the dictionaries map pyrolysis <-> gas-phase gases.
        solidReactionStoichiometry
        (
            const solidHeterogeneousReaction& R,
            const bool stoichiometric,
            const UList<scalar>& W,
            const labelUList& gasDictionary,
            const labelUList& gasDictionaryBack,
            const label nSolids
        );


    // Member Functions

        //- ODE-state index of each species the reaction touches
        const labelList& species() const
        {
            return species_;
        }

        //- Signed kg of each of species() per kg of rate
        const scalarList& coeffs() const
        {
            return coeffs_;
        }

        //- kg of gas reactant g (position in glhs()) consumed per kg of rate
        scalar agent(const label g) const
        {
            return agent_[g];
        }

        //- Reaction enthalpy per kg of rate
        scalar heat() const
        {
            return heat_;
        }

        //- kg of reactants per kg of rate
        scalar consumed() const
        {
            return consumed_;
        }
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
