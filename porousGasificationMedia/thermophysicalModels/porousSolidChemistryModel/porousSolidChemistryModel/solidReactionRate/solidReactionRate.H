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
    Foam::solidReactionRate

Description
    The rate r of one heterogeneous reaction in one cell (Zuk et al. 2022),
    in kg per m^3 of solid per s, and its derivative per reactant.

    - Eq. 4-5: r = k(T) rho prod_k x_k^n_k over the reactants (solids first,
      then gases), with rho the solid density, or the gas density if the
      reaction has no solid reactant. x_k is the solid mass fraction Ys or
      the gas-phase mass fraction.
    - Temperature cutoff: k(T) is the reaction's own kf(), zero below Tcrit.
    - Eq. 10 (diffusionLimitedReactions): 1/r = 1/r_kin + sum_a agent_a/Omega_a
      with Omega_a = ST x_a rhoG, agent_a from solidReactionStoichiometry
      (#201). A gas reactant with no supply (Omega_a = 0) stops the reaction.

    The rate reads the ODE state c (kg per m^3 of cell): x_k = c_k/(rho
    (1 - porosity)) for a solid, c_k/(rhoG porosity) for a gas, clipped at 0,
    so that it equals the frozen-field Ys, Yg at the start of a step. rho, rhoG,
    porosity and ST stay frozen per cell. rateDerivative() is the exact
    derivative of (1 - porosity) r in c, so the Jacobian matches the right-hand
    side (#200); at x = 0 under Eq. 10 it is taken as zero.

SourceFiles
    solidReactionRate.C

\*---------------------------------------------------------------------------*/

#ifndef solidReactionRate_H
#define solidReactionRate_H

#include "solidHeterogeneousReaction.H"
#include "solidReactionStoichiometry.H"
#include "volFields.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

/*---------------------------------------------------------------------------*\
                      Class solidReactionRate Declaration
\*---------------------------------------------------------------------------*/

class solidReactionRate
{
public:

    //- The frozen cell fields a rate reads besides the state
    struct cellFields
    {
        const volScalarField& rho;
        const volScalarField& rhoG;
        const volScalarField& ST;
        const volScalarField& porosity;
    };


private:

    // Private data

        const solidHeterogeneousReaction& R_;

        const solidReactionStoichiometry& st_;

        //- ODE-state index of each reactant: slhs(), then glhs()
        labelList reactants_;

        //- Reaction order of each reactant, in reactants_ order
        scalarList n_;

        const bool diffusionLimited_;

        const cellFields f_;


    // Private Member Functions

        //- Mass fraction of reactant k from the ODE state c
        scalar x(const label k, const label celli, const scalarField& c) const;

        //- Concentration c_k divides by to give x_k
        scalar xBasis(const label k, const label celli) const;

        //- Density the rate is per: solid, or gas without a solid reactant
        scalar rhoBasis(const label celli) const;

        //- Add each gas reactant's agent/Omega_diff to avKf; false if one has
        //  no supply. agent, omega: the last supplied reactant's factor and Omega.
        bool addDiffusionResistance
        (
            const label celli,
            const scalarField& c,
            scalar& avKf,
            scalar& agent,
            scalar& omega
        ) const;


public:

    // Constructors

        //- Construct for reaction R; gasDictionaryBack maps gas-phase to
        //  pyrolysis-gas index, whose ODE-state index is nSolids + that.
        solidReactionRate
        (
            const solidHeterogeneousReaction& R,
            const solidReactionStoichiometry& st,
            const labelUList& gasDictionaryBack,
            const label nSolids,
            const bool diffusionLimited,
            const cellFields& f
        );


    // Member Functions

        //- ODE-state index of each reactant, the columns of rateDerivative()
        const labelList& reactants() const
        {
            return reactants_;
        }

        //- r in cell celli at temperature T from the ODE state c
        scalar rate(const label celli, const scalar T, const scalarField& c)
            const;

        //- d((1 - porosity) r)/dc_k for each of reactants() (see Description)
        void rateDerivative
        (
            const label celli,
            const scalar T,
            const scalarField& c,
            scalarField& dr
        ) const;
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
