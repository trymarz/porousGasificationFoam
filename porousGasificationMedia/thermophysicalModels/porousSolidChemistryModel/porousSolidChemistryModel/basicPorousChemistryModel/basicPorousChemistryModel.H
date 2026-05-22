/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     | Website:  https://openfoam.org
    \\  /    A nd           | Copyright (C) 2011-2019 OpenFOAM Foundation
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
    Foam::basicPorousChemistryModel

Description
    Abstract base for the porous-medium chemistry model hierarchy.

    Owns the bookkeeping common to all chemistry models in this solver:
    the \c chemistryProperties dictionary (modified-read mode so a
    change can be picked up at runtime), the on/off \c chemistry switch,
    the initial and maximum allowed chemistry time-steps, and the
    \c deltaTChem field caching the last successful ODE step per cell.

    Templated \c New() routes through \c ChemistryModel::New(...) so a
    derived class can register itself via the
    \c porousThermoSolidChemistryModel selection table.

    Pure-virtual interface (every chemistry implementation provides):
      - \c RR(i):       cell-wise reaction rate field for specie i
                        [kg/m^3/s].
      - \c calculateRR(ri, si): per-reaction, per-species rate field.
      - \c calculate(): compute all reaction-rate fields without
                        advancing the chemistry.
      - \c solve(t0, dt): integrate the chemistry ODE for [t0, t0+dt]
                          and return the suggested next chemistry
                          time-step.
      - \c tc():        chemical time-scale field.
      - \c Qdot():      heat release rate [W/m^3].

SourceFiles
    basicPorousChemistryModel.H
    basicPorousChemistryModel.C
    basicPorousChemistryModelI.H
    basicPorousChemistryModelTemplates.C

\*---------------------------------------------------------------------------*/

#ifndef basicPorousChemistryModel_H
#define basicPorousChemistryModel_H

#include "IOdictionary.H"
#include "Switch.H"
#include "scalarField.H"
#include "volFields.H"
#include "HGSSolidThermo.H"


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

// Forward declaration of classes
class fvMesh;

/*---------------------------------------------------------------------------*\
                   class basicPorousChemistryModel Declaration
\*---------------------------------------------------------------------------*/

class basicPorousChemistryModel
:
    public IOdictionary
{
protected:

    // Protected data

        //- Reference to the mesh database
        const fvMesh& mesh_;

        //- Chemistry activation switch
        Switch chemistry_;

        //- Initial chemical time step
        const scalar deltaTChemIni_;

        //- Maximum chemical time step
        const scalar deltaTChemMax_;

        //- Latest estimation of integration step
        volScalarField::Internal deltaTChem_;


    // Protected Member Functions

        //- Return non-const access to the latest estimation of integration
        //  step, e.g. for multi-chemistry model
        inline volScalarField::Internal& deltaTChem();

        //- Correct function - updates due to mesh changes
        void correct();


public:

    //- Runtime type information
    TypeName("basicPorousChemistryModel");


    // Constructors

        //- Construct from thermo
        basicPorousChemistryModel(const HGSSolidThermo& thermo);

        //- Construct as copy (not implemented)
        basicPorousChemistryModel(const basicPorousChemistryModel&);


    // Selectors

        //- Generic New for each of the related chemistry model
        template<class ChemistryModel>
        static autoPtr<ChemistryModel> New
        (
            const typename ChemistryModel::reactionThermo& thermo,
            PtrList<volScalarField>& gasPhaseGases,
            const word& thermoName
        );


    //- Destructor
    virtual ~basicPorousChemistryModel();


    // Member Functions

        //- Return const access to the mesh database
        inline const fvMesh& mesh() const;

        //- Chemistry activation switch
        inline Switch chemistry() const;

        //- The number of species
        virtual label nSpecie() const = 0;

        //- The number of reactions
        virtual label nReaction() const = 0;

        //- Return the latest estimation of integration step
        inline const volScalarField::Internal& deltaTChem() const;


        // Functions to be derived in derived classes

            // Fields

                //- Return const access to chemical source terms [kg/m^3/s]
                virtual const volScalarField::Internal& RR
                (
                    const label i
                ) const = 0;

                //- Return access to chemical source terms [kg/m^3/s]
                virtual volScalarField::Internal& RR
                (
                    const label i
                ) = 0;

                //- Return reaction rate of the speciei in reactioni
                virtual tmp<volScalarField::Internal> calculateRR
                (
                    const label reactioni,
                    const label speciei
                ) const = 0;


            // Chemistry solution

                //- Calculates the reaction rates
                virtual void calculate() = 0;

                //- Solve the reaction system for the given time step
                //  and return the characteristic time
                virtual scalar solve(const scalar t0, const scalar deltaT) = 0;

                //- Return the chemical time scale
                virtual tmp<volScalarField> tc() const = 0;

                //- Return the heat release rate [kg/m/s^3]
                virtual tmp<volScalarField> Qdot() const = 0;


    // Member Operators

        //- Disallow default bitwise assignment
        void operator=(const basicPorousChemistryModel&) = delete;
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#include "basicPorousChemistryModelI.H"

#ifdef NoRepository
    #include "basicPorousChemistryModelTemplates.C"
#endif

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
