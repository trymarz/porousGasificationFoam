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

\*---------------------------------------------------------------------------*/

#include "solidReactionStoichiometry.H"
#include "error.H"

// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //

void Foam::solidReactionStoichiometry::add(const label i, const scalar c)
{
    forAll(species_, k)
    {
        if (species_[k] == i)
        {
            coeffs_[k] += c;
            return;
        }
    }
    species_.append(i);
    coeffs_.append(c);
}


void Foam::solidReactionStoichiometry::consume(const label i, const scalar c)
{
    add(i, -c);
    consumed_ += c;
}


void Foam::solidReactionStoichiometry::setMassBased
(
    const solidHeterogeneousReaction& R,
    const labelUList& gasDictionaryBack,
    const label nSolids
)
{
    scalar substrates = 0;
    scalar products = 0;
    scalar solidSubstrates = 0;
    scalar solidProducts = 0;

    forAll(R.grhs(), g)
    {
        products += R.grhsSto()[g];
    }
    forAll(R.glhs(), g)
    {
        substrates += R.glhsSto()[g];
    }
    forAll(R.slhs(), s)
    {
        solidSubstrates += R.slhsSto()[s];
    }
    forAll(R.srhs(), s)
    {
        solidProducts += R.srhsSto()[s];
    }

    if ((substrates + solidSubstrates == 0) || (products + solidProducts == 0))
    {
        FatalErrorInFunction
            << "Reaction:\n" << R
            << "\nis not really a reaction" << exit(FatalError);
    }

    const scalar totalSubstrates = substrates + solidSubstrates;

    if
    (
        !(totalSubstrates > 0)
     || !(mag(totalSubstrates - (products + solidProducts)) < SMALL)
    )
    {
        FatalErrorInFunction
            << "Reaction:\n" << R
            << "\nviolates mass conservation" << exit(FatalError);
    }

    // Eq. 6 with the rate per kg of solid substrate (#201): every
    // coefficient over the solid reactants' sum, gases included. A
    // reaction with no solid reactant keeps its per-kg-of-gas basis.
    const scalar rateBasis =
        solidSubstrates > 0 ? solidSubstrates : totalSubstrates;

    forAll(R.slhs(), s)
    {
        consume(R.slhs()[s], R.slhsSto()[s]/rateBasis);
    }
    forAll(R.glhs(), g)
    {
        // Departs from the literal Eq. 10 (#201): the agent is consumed at
        // xi_a*r, so its diffusion resistance is xi_a/Omega_diff.
        agent_[g] = R.glhsSto()[g]/rateBasis;
        consume(gasDictionaryBack[R.glhs()[g]] + nSolids, agent_[g]);
    }
    forAll(R.srhs(), s)
    {
        add(R.srhs()[s], R.srhsSto()[s]/rateBasis);
    }
    forAll(R.grhs(), g)
    {
        add(R.grhs()[g] + nSolids, R.grhsSto()[g]/rateBasis);
    }
}


void Foam::solidReactionStoichiometry::setStoichiometric
(
    const solidHeterogeneousReaction& R,
    const UList<scalar>& W,
    const labelUList& gasDictionary,
    const labelUList& gasDictionaryBack,
    const label nSolids
)
{
    // Gas sums are molar-mass weighted: kg of gas per reaction "mole".
    scalar substrates = 0;
    scalar products = 0;
    scalar solidSubstrates = 0;
    scalar solidProducts = 0;

    forAll(R.grhs(), g)
    {
        products += W[gasDictionary[R.grhs()[g]]]*R.grhsSto()[g];
    }
    forAll(R.glhs(), g)
    {
        substrates += W[R.glhs()[g]]*R.glhsSto()[g];
    }
    forAll(R.slhs(), s)
    {
        solidSubstrates += R.slhsSto()[s];
    }
    forAll(R.srhs(), s)
    {
        solidProducts += R.srhsSto()[s];
    }

    if ((substrates + solidSubstrates == 0) || (products + solidProducts == 0))
    {
        FatalErrorInFunction
            << "Reaction:\n" << R
            << "\nis not really a reaction" << exit(FatalError);
    }

    if (solidSubstrates > 0 && solidSubstrates != solidProducts)
    {
        // Solids per kg of solid reactant; the gases carry the solid mass
        // change, (1 - sr) kg per kg of solid reactant.
        const scalar sr = solidProducts/solidSubstrates;
        const scalar massCoefficient = 1./(products - substrates);

        forAll(R.slhs(), s)
        {
            consume(R.slhs()[s], R.slhsSto()[s]/solidSubstrates);
        }
        forAll(R.srhs(), s)
        {
            add(R.srhs()[s], R.srhsSto()[s]/solidSubstrates);
        }
        forAll(R.glhs(), g)
        {
            consume
            (
                gasDictionaryBack[R.glhs()[g]] + nSolids,
                (1.0 - sr)*massCoefficient*W[R.glhs()[g]]*R.glhsSto()[g]
            );
        }
        forAll(R.grhs(), g)
        {
            add
            (
                R.grhs()[g] + nSolids,
                (1.0 - sr)*massCoefficient
               *W[gasDictionary[R.grhs()[g]]]*R.grhsSto()[g]
            );
        }
    }
    else if (solidSubstrates < solidProducts)
    {
        // No solid reactant: per kg of gas reactant, and the solid products
        // take the gas mass lost, (1 - sr) kg per kg.
        const scalar sr = products/substrates;

        forAll(R.glhs(), g)
        {
            consume
            (
                gasDictionaryBack[R.glhs()[g]] + nSolids,
                W[R.glhs()[g]]*R.glhsSto()[g]/substrates
            );
        }
        forAll(R.grhs(), g)
        {
            add
            (
                R.grhs()[g] + nSolids,
                sr*W[gasDictionary[R.grhs()[g]]]*R.grhsSto()[g]/products
            );
        }
        forAll(R.srhs(), s)
        {
            add(R.srhs()[s], (1-sr)*R.srhsSto()[s]/solidProducts);
        }
    }
    else if (products == substrates)
    {
        // Solid mass is conserved, and so is the gas mass.
        forAll(R.slhs(), s)
        {
            consume(R.slhs()[s], R.slhsSto()[s]/solidSubstrates);
        }
        forAll(R.srhs(), s)
        {
            add(R.srhs()[s], R.srhsSto()[s]/solidProducts);
        }
        if (products > 0)
        {
            forAll(R.glhs(), g)
            {
                consume
                (
                    gasDictionaryBack[R.glhs()[g]] + nSolids,
                    W[R.glhs()[g]]*R.glhsSto()[g]/substrates
                );
            }
            forAll(R.grhs(), g)
            {
                add
                (
                    R.grhs()[g] + nSolids,
                    W[gasDictionary[R.grhs()[g]]]*R.grhsSto()[g]/products
                );
            }
        }
    }
    else
    {
        FatalErrorInFunction
            << "Reaction:\n" << R
            << "\ntype is not implemented" << exit(FatalError);
    }
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::solidReactionStoichiometry::solidReactionStoichiometry
(
    const solidHeterogeneousReaction& R,
    const bool stoichiometric,
    const UList<scalar>& W,
    const labelUList& gasDictionary,
    const labelUList& gasDictionaryBack,
    const label nSolids
)
:
    species_(),
    coeffs_(),
    agent_(R.glhs().size(), 1.0),
    heat_(R.heatReact()),
    consumed_(0)
{
    if (stoichiometric)
    {
        setStoichiometric(R, W, gasDictionary, gasDictionaryBack, nSolids);
    }
    else
    {
        setMassBased(R, gasDictionaryBack, nSolids);
    }
}


// ************************************************************************* //
