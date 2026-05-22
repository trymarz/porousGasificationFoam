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
    Foam::porousThermoSolidChemistryModel

Description
    Chemistry-model layer templated on the solid thermo package.

    Sits between \c basicPorousChemistryModel (which is thermo-agnostic)
    and the concrete \c ODESolidHeterogeneousChemistryModel (which adds
    the per-reaction ODE). This class adds:

      - a typed reference to the solid thermo package (HGSSolidThermo
        derivative) so derived classes can read densities, mass
        fractions, K, Cp, etc.;
      - a runtime selection table keyed on \c (solidThermo, gasPhaseGases)
        with the thermo type as the entry name — populated by
        \c makeChemistrySolver.H instantiations;
      - the additional pure-virtual interface needed by
        \c volPyrolysis::evolveRegion():
          * \c RRs(i), \c RRs(): solid-species and total solid-to-gas
                                  mass source [kg/m^3/s],
          * \c RRg(i), \c RRg(): per-gas-species and total gas-side
                                  source,
          * \c gasHs(p, T, i):   sensible enthalpy of gas i [J/kg],
          * \c Sh():             heat-of-reaction source for the
                                  solid energy equation [W/m^3],
          * \c gasTable():       speciesTable for the pyrolysis gases,
          * \c setCellReacting:  enable/disable chemistry per cell
                                  (volPyrolysis uses this mask),
          * \c RRpor(T):         porosity-growth rate [1/s] driven
                                  by the solid-to-gas conversion.

SourceFiles
    porousThermoSolidChemistryModel.H
    porousThermoSolidChemistryModel.C
    porousThermoSolidChemistryModels.C
    porousThermoSolidChemistryModelI.H

\*---------------------------------------------------------------------------*/

#ifndef porousThermoSolidChemistryModel_H
#define porousThermoSolidChemistryModel_H

#include "basicPorousChemistryModel.H"
#include "autoPtr.H"
#include "runTimeSelectionTables.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

// Forward declaration of classes
class fvMesh;

/*---------------------------------------------------------------------------*\
                     class porousThermoSolidChemistryModel Declaration
\*---------------------------------------------------------------------------*/

template<class SolidThermo>
class porousThermoSolidChemistryModel
:
    public basicPorousChemistryModel
{
protected:

    // Protected data

        //- Thermo
        const SolidThermo& solidThermo_;


public:

    //- Runtime type information
    TypeName("porousThermoSolidChemistryModel");


    //- Thermo type
    typedef SolidThermo reactionThermo;


    //- Declare run-time constructor selection tables
    declareRunTimeSelectionTable
    (
        autoPtr,
        porousThermoSolidChemistryModel,
        thermo,
        (
            const SolidThermo& thermo,
            PtrList<volScalarField>& gasPhaseGases
        ),
        (thermo, gasPhaseGases)
    );


    // Constructors

        //- Construct from thermo
        porousThermoSolidChemistryModel(const SolidThermo& thermo);

    //- Selector
    static autoPtr<porousThermoSolidChemistryModel<SolidThermo>> New
    (
        const SolidThermo& thermo,
        PtrList<volScalarField>& gasPhaseGases,
        const word thermoName
    );


    //- Destructor
    virtual ~porousThermoSolidChemistryModel();


    // Member Functions

        //- Return access to the solid thermo package
        //inline SolidThermo& solidThermo();

        //- Return access to the solid's thermo package
        inline const SolidThermo& solidThermo() const;

        //- Return total solids mass source term [kg/m3/s]
        virtual tmp<volScalarField> RRs() const = 0;

        //- Return total gases mass source term [kg/m3/s]
        virtual tmp<volScalarField> RRg() const = 0;

        //- Return chemical source terms for solids [kg/m3/s]
        virtual tmp<volScalarField> RRs(const label i) const = 0;

        //- Return chemical source terms for gases [kg/m3/s]
        virtual tmp<volScalarField> RRg(const label i) const = 0;

        //- Return sensible enthalpy for gas i [J/Kg]
        virtual tmp<volScalarField> gasHs
        (
            const volScalarField& p,
            const volScalarField& T,
            const label i
        ) const = 0;

        //- Return source for enthalpy equation [kg/m/s3]
        virtual tmp<volScalarField> Sh() const = 0;

        //- Return specie Table for gases
        virtual const speciesTable& gasTable() const = 0;

        //- Set reacting status of cell, cellI
        virtual void setCellReacting(const label cellI, const bool active) = 0;

        //- Calculates the reaction rates
        virtual void calculate() = 0;

        //- Return source for porosity equation [1/s]
        virtual tmp<volScalarField> RRpor(const volScalarField T) const = 0;

};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#ifdef NoRepository
    #include "porousThermoSolidChemistryModel.C"
#endif

#include "porousThermoSolidChemistryModelI.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
