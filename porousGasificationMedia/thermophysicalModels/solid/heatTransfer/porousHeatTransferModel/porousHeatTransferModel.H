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
    Foam::porousHeatTransferModel

Description
    Interface of the base class for the convective heat transfer models
    between the gas and solid.

    The convective heat transfer between the gas and solid is approximated as:
    \f[
        q_{CONV} = h_{CONV}  \sum (T^{fluid} - T^{solid}),
    \f]
    where:
        - \f$ h_{CONV} \f$ -- Convective heat transfer coefficient
        - \f$ \sum \f$ -- Surface area to volume ratio
        - T -- temperature

SourceFiles
    porousHeatTransferModel.C

\*---------------------------------------------------------------------------*/

#ifndef porousHeatTransferModel_H
#define porousHeatTransferModel_H

#include "primitiveFieldsFwd.H"
#include "Time.H"
#include "IOdictionary.H"
#include "volFieldsFwd.H"
#include "surfaceFieldsFwd.H"
#include "fvMatricesFwd.H"
#include "autoPtr.H"
#include "runTimeSelectionTables.H"
#include "fvMesh.H"
#include "fluidThermo.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

/*---------------------------------------------------------------------------*\
                           Class porousHeatTransferModel Declaration
\*---------------------------------------------------------------------------*/

//- The porousHeatTransferModel class
class porousHeatTransferModel
:
    public IOdictionary
{

protected:

    // Protected data

        //- Reference to the time database..
        const Time& runTime_;

        //- Reference to the mesh database.
        const fvMesh& mesh_;

        //- Porosity.
        const volScalarField& porosity_;

        //- Initial porosity.
        const volScalarField& initialPorosity_;

private:

    // Private Member Functions

        //- Disallow default bitwise copy construct
        porousHeatTransferModel(const porousHeatTransferModel&);

        //- Disallow default bitwise assignment
        void operator=(const porousHeatTransferModel&);


public:

    //- Runtime type information
    TypeName("porousHeatTransferModel");

    // Declare run-time New selection table
        declareRunTimeNewSelectionTable
        (
            autoPtr,
            porousHeatTransferModel,
            porosity,
            (
                const volScalarField& porosity, const volScalarField& initialPorosity
            ),
            (porosity,initialPorosity)
        );


    // Constructors

        //- Construct from porosity
        porousHeatTransferModel
        (
            const volScalarField& porosity,
            const volScalarField& initialPorosity
        );

    // Selectors

        //- Return a reference to the selected heatTransfer model.
        static autoPtr<porousHeatTransferModel> New
        (
            const volScalarField& porosity,
            const volScalarField& initialPorosity
        );


    //- Destructor
    virtual ~porousHeatTransferModel()
    {}


    // Member Functions

        //- Return the porosity field
        inline const volScalarField& porosity() const
        {
            return porosity_;
        }

        //- Return the initial porosity field
        inline const volScalarField& initialPorosity() const
        {
            return initialPorosity_;
        }

        //- Return the heatTransfer coefficient
        virtual tmp<volScalarField> CONV() const = 0;

        //- Read heatTransferProperties dictionary
        virtual bool read() = 0;

        //- Default dummy write function, does nothing
        virtual bool writeData(Ostream&) const
        {
            return true;
        }
};

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
