/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     | Website:  https://openfoam.org
    \\  /    A nd           | Copyright (C) 2011-2020 OpenFOAM Foundation
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
    Foam::HGSSolidThermo

Description
    Abstract base for the PGF solid-phase thermo package
    ("Heterogeneous Gas-Solid" — HGS).

    Owns the per-cell solid-phase fields shared by every concrete
    implementation: temperature \c T_, density \c rho_, absorption
    coefficient \c kappa_, scatter coefficient \c sigmaS_, emissivity
    \c emissivity_. The dictionary it reads is
    \c constant/solidThermophysicalProperties.

    Three runtime selection tables are declared so callers can pick
    the construction signature they need:
      - \c mesh:       (fvMesh) only — empty mixture, used in tests;
      - \c dictionary: (fvMesh, dictionary) — the standard route;
      - \c gasPhase:   (fvMesh, dictionary, gasPhaseGases) — required
                       when the mixture contains heterogeneous
                       reactions referencing gas species (the main
                       solver path).

    Pure-virtual interface a derived class implements:
      - \c correct(): recompute K, hs, etc. from the current
        composition (called by volPyrolysis after solveSpeciesMass);
      - \c Cp(), \c Hf(): per-cell specific heat and formation
        enthalpy [J/kg/K], [J/kg];
      - per-patch overloads \c rho(patchI), \c Cp(patchI), \c K(patchI),
        \c kappa(patchI), \c sigmaS(patchI), \c emissivity(patchI),
        \c Hf(patchI) for boundary-condition access.

    The default \c composition() returns a base \c basicSolidMixture.
    The concrete \c HGSSolidMixtureThermo<MixtureType> overrides this
    to return the typed mixture directly (CRTP-style).

    \c directionalK() exposes a per-cell symmetric tensor for
    anisotropic conduction. The default wraps the isotropic K as a
    symmTensor; override in derived classes that need anisotropy.

SourceFiles
    HGSSolidThermo.H
    HGSSolidThermo.C
    HGSSolidThermoNew.C

\*---------------------------------------------------------------------------*/

#ifndef HGSSolidThermo_H
#define HGSSolidThermo_H

#include "volFields.H"
#include "runTimeSelectionTables.H"
#include "fvMesh.H"
#include "typeInfo.H"
#include "IOdictionary.H"
#include "autoPtr.H"
#include "wordIOList.H"
#include "basicSolidMixture.H"


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

/*---------------------------------------------------------------------------*\
                        Class HGSSolidThermo Declaration
\*---------------------------------------------------------------------------*/

class HGSSolidThermo
:
    public IOdictionary
{

protected:

    // Protected data

                 
        //- Mesh reference
        const fvMesh& mesh_;
        
        // Fields

             //- Temperature [K]
            volScalarField T_;

            //- Density [kg/m3]
            volScalarField rho_;

            //- Absorption coefficient [1/m]
            volScalarField kappa_;

            //- Scatter coeffcient [1/m]
            volScalarField sigmaS_;

            //- Emissivity coeffcient []
            volScalarField emissivity_;
            
            
    // Protected Member Functions

        //- Construct as copy (not implemented)
        HGSSolidThermo(const HGSSolidThermo&);

       
public:

    //- Runtime type information
    TypeName("HGSSolidThermo");


    //- Declare run-time constructor selection table
    declareRunTimeSelectionTable
    (
        autoPtr,
        HGSSolidThermo,
        mesh,
        (const fvMesh& mesh),
        (mesh)
    );
    
    declareRunTimeSelectionTable
    (
        autoPtr,
        HGSSolidThermo,
        dictionary,
        (const fvMesh& mesh, const dictionary& dict),
        (mesh, dict)
    );
    
    declareRunTimeSelectionTable
    (
        autoPtr,
        HGSSolidThermo,
        gasPhase,
        (
            const fvMesh& mesh,
            const dictionary& dict,
            const PtrList<volScalarField>& gasPhaseGases
        ),
        (mesh, dict, gasPhaseGases)
    );

    // Constructors

        //- Construct from mesh 
        HGSSolidThermo(const fvMesh&);

        //- Construct from mesh and dictionary and phase name
        HGSSolidThermo(const fvMesh&, const dictionary&);
        
        //- Construct from mesh, dictionary and phase name
        HGSSolidThermo
        (
            const fvMesh& mesh,
            const dictionary& dict,
            const PtrList<volScalarField>& gasPhaseGases
        );

    // Selectors
    
        //- Return a pointer to a new basicHGSSolidThermo created from
        //  the solidThermophysicalProperties dictionary
        static autoPtr<HGSSolidThermo> New(const fvMesh&);

        //- Return a pointer to a new basicHGSSolidThermo created from
        //  local dictionary
        static autoPtr<HGSSolidThermo> New(const fvMesh&, const dictionary&);
        
        static autoPtr<HGSSolidThermo> New
        (            
            const fvMesh& mesh,
            const dictionary& dict,
            const PtrList<volScalarField>& gasPhaseGases
        );

    //- Destructor
    virtual ~HGSSolidThermo();


    // Member Functions

        //- Update properties
        virtual void correct() = 0;
              
        //- Return the composition of the solid mixture
        virtual basicSolidMixture& composition();

        //- Return the composition of the solid mixture
        virtual const basicSolidMixture& composition() const;


        // Access to thermodynamic state variables

           //- mesh
            const fvMesh& mesh() const;

            //- Density [kg/m^3]
            const volScalarField& rho() const;

            //- Thermal conductivity [W/m/K]
            virtual const volScalarField& K() const;

            //- Thermal conductivity [W/m/K]
            virtual const volSymmTensorField& directionalK() const;

            //- Absorption coefficient [1/m]
            const volScalarField& kappa() const;

            //- Emissivity coefficient
            const volScalarField& sigmaS() const;

            //- Emissivity coefficient [1/m]
            const volScalarField& emissivity() const;

            //- Temperature [K]
            const volScalarField& T() const;

            //- non-const access for T
            volScalarField& T();

            //- non-const access for rho
            volScalarField& rho();


        // Derived thermal properties

            //- Specific heat capacity [J/kg/K]
            virtual tmp<volScalarField> Cp() const = 0;

            //- Sensible enthalpy [J/kg]
            virtual tmp<volScalarField> hs() const;

           //- Heat of formation [J/kg]
            virtual tmp<volScalarField> Hf() const = 0;


        // Per patch calculation

            //- Density [kg/m3]
            virtual tmp<scalarField> rho(const label patchI) const = 0;

            //- Specific heat capacity [J/kg/K)]
            virtual tmp<scalarField> Cp(const label patchI) const = 0;

            //- Sensible enthalpy [J/kg]
            virtual tmp<scalarField> hs(const label patchI) const;

            //- Thermal conductivity [W//m/K]
            virtual tmp<scalarField> K(const label patchI) const;

            //- Thermal conductivity [W//m/K]
            virtual tmp<symmTensorField> directionalK(const label) const;

            //- Heat of formation [J/kg]
            virtual tmp<scalarField> Hf(const label patchI) const = 0;

            //- Scatter coefficient [1/m]
            virtual tmp<scalarField> sigmaS(const label) const = 0;

            //- Absorption coefficient [1/m]
            virtual tmp<scalarField> kappa(const label) const = 0;

            //- Emissivity []
            virtual tmp<scalarField> emissivity(const label) const = 0;

    // I-O

        //- Write the HGSSolidThermo properties
        virtual bool writeData(Ostream& os) const = 0;

        //- Read thermophysicalProperties dictionary
        virtual bool read() = 0;

        //- Ostream Operator
        friend Ostream& operator<<(Ostream& os, const HGSSolidThermo& s);
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
