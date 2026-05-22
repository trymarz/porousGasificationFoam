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
    Foam::heterogeneousRadiationModel

Description
    Abstract base for the heterogeneous (gas + solid) radiation model.

    Two field references it always owns: the gas temperature \c T_
    (inherited from the OpenFOAM \c radiation::radiationModel base) and
    the solid temperature \c Ts_. Plus a sub-model for the absorption /
    emission coefficients (\c heterogeneousAbsorptionEmissionModel),
    selected from \c constant/radiationProperties.

    Two RTS tables: one keyed on \c T only (used by tests / no-radiation
    branch), one keyed on (\c T, porosityF, surfF, Ts) — the main
    solver always uses the second.

    Pure-virtual surface used by the main solver:
      - \c solidSh() — volumetric radiative source for the solid
        energy equation [W/m^3]. Published from
        \c heterogeneousRadiationModel via \c radiationF in
        radiation.H (see porousGasificationFoam/radiation.H).

    Concrete implementations:
      - \c heterogeneousNoRadiation (type name "none") — zero source;
      - \c heterogeneousMeanTemp — surface-only mean-temperature model
        for thin beds with cheap radiation;
      - \c heterogeneousP1 — P1 diffusion approximation, full
        volumetric, used for thick beds.

SourceFiles
    heterogeneousRadiationModel.H
    heterogeneousRadiationModel.C
    heterogeneousRadiationModelNew.C

\*---------------------------------------------------------------------------*/

#ifndef heterogeneousRadiationModel_H
#define heterogeneousRadiationModel_H

#include "radiationModel.H"
#include "autoPtr.H"
#include "runTimeSelectionTables.H"
#include "volFields.H"
#include "fvMatricesFwd.H"
#include "Switch.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

// Forward declaration of classes
class basicThermo;
class fvMesh;

namespace radiationModels
{
class heterogeneousAbsorptionEmissionModel;
}

namespace radiation
{
class scatterModel;
class sootModel;
}

/*---------------------------------------------------------------------------*\
                 Class heterogeneousRadiationModel Declaration
\*---------------------------------------------------------------------------*/

class heterogeneousRadiationModel
:
    public radiation::radiationModel
{
protected:

    // Protected data

        //- Reference to the solid temperature field
        const volScalarField& Ts_;


        // References to the radiation sub-models

            //- Absorption/emission model
            autoPtr<radiationModels::heterogeneousAbsorptionEmissionModel>
                heterogeneousAbsorptionEmission_;

private:

    // Private Member Functions

        //- Initialise
        void initialise();


public:

    //- Runtime type information
    TypeName("heterogeneousRadiationModel");


    // Declare runtime constructor selection table

        declareRunTimeSelectionTable
        (
            autoPtr,
            heterogeneousRadiationModel,
            T,
            (
                const volScalarField& T
            ),
            (T)
        );

        declareRunTimeSelectionTable
        (
            autoPtr,
            heterogeneousRadiationModel,
            porosity,
            ( 
                const volScalarField& T,
                const volScalarField& porosityF,
                const volScalarField& surfF,
                const volScalarField& Ts
            ),
            (T,porosityF,surfF,Ts)
        );

    // Constructors

        //- Null constructor
        heterogeneousRadiationModel(const volScalarField& T);

        //- Construct from components
        heterogeneousRadiationModel(const word& type, const volScalarField& T);

        //- Construct from porosity, surface and solid temperature
        heterogeneousRadiationModel
        (
            const volScalarField& T,
            const volScalarField& porosityF,
            const List<label>& surfF,
            const volScalarField& Ts
        );

    // Selectors

        //- Return a reference to the selected radiation model
        static autoPtr<heterogeneousRadiationModel> New(const volScalarField& T);

        //- Return a reference to the selected radiation model
        static autoPtr<heterogeneousRadiationModel> New
        (
            const volScalarField& T,
            const volScalarField& porosityF,
            const volScalarField& surfF,
            const volScalarField& Ts
        );


    //- Destructor
    virtual ~heterogeneousRadiationModel();


    // Member Functions
        // Access

            //- Enthalpy source term for solid
            virtual tmp<volScalarField> solidSh() const = 0;

            //- Access to absorptionEmission model
            const radiationModels::heterogeneousAbsorptionEmissionModel&
                heterogeneousAbsorptionEmission() const;

            //- Number of bands
            label nBands() const;


    // Member Operators

        //- Disallow default bitwise assignment
        void operator=(const heterogeneousRadiationModel&) = delete;
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
