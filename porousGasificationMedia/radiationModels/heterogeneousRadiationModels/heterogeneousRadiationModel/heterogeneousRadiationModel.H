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
    Top level model for heterogeneous radiation modelling

SourceFiles
    heterogeneousRadiationModel.C

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
                const volScalarField& cellOnBedBorder,
                const volScalarField& Ts,
                const volScalarField& solidSourceTermsAdmissible
            ),
            (T,porosityF,cellOnBedBorder,Ts,solidSourceTermsAdmissible)
        );

    // Constructors

        //- Null constructor
        heterogeneousRadiationModel(const volScalarField& T);

        //- Construct from components
        heterogeneousRadiationModel(const word& type, const volScalarField& T);

        //- Construct from components, radiating the solid at its own Ts
        //  rather than at the gas temperature the two-argument form binds.
        heterogeneousRadiationModel
        (
            const word& type,
            const volScalarField& T,
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
            const volScalarField& cellOnBedBorder,
            const volScalarField& Ts,
            const volScalarField& solidSourceTermsAdmissible
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
