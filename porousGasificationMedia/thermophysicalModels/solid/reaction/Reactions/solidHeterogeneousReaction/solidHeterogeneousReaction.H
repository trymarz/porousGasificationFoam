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
    Foam::solidHeterogeneousReaction

Description
    Parsed representation of a heterogeneous (S + G = S + G) reaction
    — the building block of PGF's reaction list.

    Stores per-side index lists into \c components_ (solid species)
    and \c pyrolisisGases_ (gas species), each paired with a list of
    stoichiometric coefficients:
      - \c slhs_, \c slhsSto_:  solid LHS,
      - \c glhs_, \c glhsSto_:  gas LHS,
      - \c srhs_, \c srhsSto_:  solid RHS,
      - \c grhs_, \c grhsSto_:  gas RHS.

    Pure virtual: \c kf(T, p, c) returns the forward rate constant,
    \c heatReact() the user-specified heat of reaction, \c nReact()
    the per-reactant order list. The rate is computed by the wrapping
    \c IrreversibleSolidHeterogeneousReaction<ReactionRate> template,
    which combines this stoichiometry with a chosen rate law
    (Arrhenius, ModArr, TemperatureArrhenius, Const — see
    solid/reaction/reactionRate/).

    The reaction list is read by
    \c reactingSolidHeterogeneousMixture::read from the
    \c solidReactions entry of \c constant/chemistryProperties.
    Format per reaction (three lines):

    \code
    <typeName>
    n_S1 S1 + n_G1 G1 = n_S2 S2 + n_G2 G2
    (A Ta Tcrit heatOfReaction n1 n2 ...)
    \endcode

    The bound \c gasPhaseGases_ reference lets the rate law evaluate
    the gas-side concentration term using the current gas mass
    fractions / densities.

SourceFiles
    solidHeterogeneousReaction.H
    solidHeterogeneousReaction.C
    solidHeterogeneousReactionI.H

\*---------------------------------------------------------------------------*/

#ifndef solidHeterogeneousReaction_H
#define solidHeterogeneousReaction_H

#include "speciesTable.H"
#include "scalarField.H"
#include "typeInfo.H"
#include "runTimeSelectionTables.H"

#include "volFieldsFwd.H"
#include "fvCFD.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

// Forward declaration of friend functions and operators
class solidHeterogeneousReaction;

inline Ostream& operator<<(Ostream&, const solidHeterogeneousReaction&);

/*---------------------------------------------------------------------------*\
                        Class solidHeterogeneousReactionDeclaration
\*---------------------------------------------------------------------------*/

class solidHeterogeneousReaction
{

private:

    // Private data

        //- List of gas species from gas phase available for gasification reactions
        const PtrList<volScalarField>& gasPhaseGases_;

        //- List of solid names present in reaction system
        const speciesTable& components_;

        //- List of gas species present in reaction system
        speciesTable pyrolisisGases_;

        //- Solid components index for the left-hand-side of the reaction
        List<label> slhs_;

        //- Solid mass coefficients for left-hand-side of the reaction
        List<scalar> slhsSto_;

        //- Gas components index for the left-hand-side of the reaction
        List<label> glhs_;

        //- Gas stoichmietry coefficients for left-hand-side of the reaction
        List<scalar> glhsSto_;

        //- Solid components index for the right-hand-side of the reaction
        List<label> srhs_;

        //- Solid mass coefficients for left-hand-side of the reaction
        List<scalar> srhsSto_;

        //- Specie index for the right-hand-side of the reaction
        List<label> grhs_;

        //- Gas stoichmietry coefficients for right-hand-side of the reaction
        List<scalar> grhsSto_;

    // Private Member Functions

        //- Set rhs and lhs of the reaction
        void setLRhs(Istream&);

        //- Look for the component index in the reaction
        label componentIndex(bool& isGas, token t);

public:

    //- Runtime type information
    TypeName("HeterogeneousReaction");

    // Declare run-time constructor selection tables

        declareRunTimeSelectionTable
        (
            autoPtr,
            solidHeterogeneousReaction,
            Istream,
            (
                const PtrList<volScalarField>& gasPhaseGases,
                const speciesTable& components,
                Istream& is,
                const speciesTable& pyrolysisGases
            ),
            (gasPhaseGases, components, is, pyrolysisGases)
        );


    // Public classes

        //- Class used for the read-construction of PtrLists of reaction
        class iNew
        {
            const PtrList<volScalarField>& gasPhaseGases_;
            const speciesTable& components_;
            speciesTable pyrolisisGases_;

        public:

            iNew
            (
                const PtrList<volScalarField>& gasPhaseGases,
                const speciesTable& components,
                Istream& pyrolisisGases
            )
            :
                gasPhaseGases_(gasPhaseGases),
                components_(components),
                pyrolisisGases_(pyrolisisGases)
            {}

            autoPtr<solidHeterogeneousReaction> operator()(Istream& is) const
            {
                return autoPtr<solidHeterogeneousReaction>
                (
                    solidHeterogeneousReaction::New
                    (
                        gasPhaseGases_,
                        components_,
                        is,
                        pyrolisisGases_
                    )
                );
            }
        };


    // Constructors

        //- Construct from components
        solidHeterogeneousReaction
        (
            const PtrList<volScalarField>& gasPhaseGases,
            const speciesTable& components,
            const speciesTable& pyrolisisGases,
            const List<label>& slhs,
            const List<label>& glhs,
            const List<label>& srhs,
            const List<label>& grhs
        );

        //- Construct as copy given new speciesTable
        solidHeterogeneousReaction
        (
            const solidHeterogeneousReaction&,
            const PtrList<volScalarField>& gasPhaseGases,
            const speciesTable& components,
            const speciesTable& pyrolisisGases
        );

        //- Construct from Istream
        solidHeterogeneousReaction
        (
            const PtrList<volScalarField>& gasPhaseGases,
            const speciesTable& components,
            Istream& is,
            const speciesTable& pyrolisisGases
        );

        //- Construct and return a clone
        virtual autoPtr<solidHeterogeneousReaction> clone() const
        {
            return autoPtr<solidHeterogeneousReaction>
            (
                new solidHeterogeneousReaction(*this)
            );
        }


    // Selectors

        //- Return a pointer to a new patchField created on freestore from input
        static autoPtr<solidHeterogeneousReaction> New
        (
            const PtrList<volScalarField>& gasPhaseGases,
            const speciesTable& components,
            Istream&,
            const speciesTable& pyrolisisGases
        );


    //- Destructor
    virtual ~solidHeterogeneousReaction()
    {}


    // Member Functions

        // Access

            inline const List<label>& slhs() const;

            inline const List<scalar>& slhsSto() const;

            inline const List<label>& glhs() const;

            inline const List<scalar>& glhsSto() const;

            inline const List<label>& srhs() const;

            inline const List<scalar>& srhsSto() const;

            inline const List<label>& grhs() const;

            inline const List<scalar>& grhsSto() const;

            inline const speciesTable& pyrolisisGases() const;


        //- solidHeterogeneousReactionrate coefficients

            virtual scalar kf
            (
                const scalar T,
                const scalar p,
                const scalarField& c
            ) const;

            virtual scalar heatReact() const;

            virtual List<scalar> nReact() const;

        //- Write
        virtual void write(Ostream&) const;


    // Ostream Operator

        friend Ostream& operator<<
        (
            Ostream&,
            const solidHeterogeneousReaction&
        );
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#include "solidHeterogeneousReactionI.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
