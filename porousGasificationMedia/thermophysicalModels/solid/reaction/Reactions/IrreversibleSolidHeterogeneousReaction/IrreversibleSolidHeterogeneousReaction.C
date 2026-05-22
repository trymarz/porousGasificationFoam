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
    Template implementation of \c IrreversibleSolidHeterogeneousReaction.
    See the class Description in the .H.

    Two constructors:
      - Programmatic: takes a pre-built \c solidHeterogeneousReaction
        and a rate object.
      - Stream: reads the stoichiometry via
        \c solidHeterogeneousReaction(Istream), then reads the rate
        object \c k_ from the stream followed by \c heatReact_
        (J/kg, heat of reaction, positive = exothermic) and per-
        reactant reaction orders \c nReact_.
    \c kf() delegates to the rate object. \c write() serialises the
    reaction in human-readable form for diagnostic output.

\*---------------------------------------------------------------------------*/

#include "IrreversibleSolidHeterogeneousReaction.H"

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

template<class ReactionRate>
Foam::IrreversibleSolidHeterogeneousReaction<ReactionRate>::IrreversibleSolidHeterogeneousReaction
(
    const solidHeterogeneousReaction& reaction,
    const ReactionRate& k,
    const scalar nReact
)
:
    solidHeterogeneousReaction(reaction),
    k_(k),
    heatReact_(List<scalar>()),
    nReact_(nReact)
{}


template<class ReactionRate>
Foam::IrreversibleSolidHeterogeneousReaction<ReactionRate>::IrreversibleSolidHeterogeneousReaction
(
    const PtrList<volScalarField>& gasPhaseGases,
    const speciesTable& components,
    Istream& is,
    const speciesTable& pyrolysisGases
)
:
    solidHeterogeneousReaction(gasPhaseGases, components, is, pyrolysisGases),
    k_(components, is),
    heatReact_(readScalar(is)),
    nReact_(List<scalar>())
{
    List<int> lhs;
    DynamicList<scalar> dnReact;
    lhs.append(slhs());
    lhs.append(glhs());
    forAll(lhs,i)
    {
        dnReact.append(readScalar(is));
    }
    is.readEnd("solidArrheniusReactionRate(Istream&)");
    nReact_ = dnReact.shrink();
}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

template<class ReactionRate>
Foam::scalar Foam::IrreversibleSolidHeterogeneousReaction<ReactionRate>::kf
(
    const scalar T,
    const scalar p,
    const scalarField& c
) const
{  
    return k_(T, p, c);
}

template<class ReactionRate>
Foam::scalar Foam::IrreversibleSolidHeterogeneousReaction<ReactionRate>::heatReact() const
{
    return heatReact_;
}

template<class ReactionRate>
Foam::List<scalar> Foam::IrreversibleSolidHeterogeneousReaction<ReactionRate>::nReact() const
{
    return nReact_;
}

template<class ReactionRate>
void Foam::IrreversibleSolidHeterogeneousReaction<ReactionRate>::write
(
    Ostream& os
) const
{
    solidHeterogeneousReaction::write(os);
    os  << token::SPACE << "Reaction order: " << nReact_ << nl 
        << token::SPACE << "Energy of reaction (if applicable): " << heatReact_ << " J/kg" 
        << token::SPACE << nl << k_;
}


// ************************************************************************* //
