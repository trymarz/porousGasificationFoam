/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     | Website:  https://openfoam.org
    \\  /    A nd           | Copyright (C) 2012-2019 OpenFOAM Foundation
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
    Foam::fieldPorosityModel

Description
    Porous resistance model driven by a per-cell porosity field and an
    anisotropic Darcy tensor field, with an optional global Forchheimer
    coefficient.

    Per cell with \c 0 <= porosityF < 1, the per-unit-volume momentum
    sink added to UEqn is
    \f[
        S_U = -\Big[\mu + f\, \rho\, |U|\, \sqrt{3}/|D_f|\Big]\,
              D_f \cdot U
    \f]
    where:
      - \f$D_f\f$ is the (anisotropic) Darcy resistance tensor field
        \c Df read from time 0 — large in gas-only regions to forbid
        flow, smaller in porous regions based on permeability;
      - \f$\mu\f$ is the gas dynamic viscosity (looked up by
        \c thermo:mu or computed as \c rho*nu);
      - \f$f\f$ is the Forchheimer scalar from
        \c constant/porosityProperties (default 0 — pure Darcy).

    The isotropic trace of the resulting tensor goes into the UEqn
    diagonal (\c Udiag) and the deviatoric part into the source
    (\c Usource) — the standard implicit/explicit split for
    anisotropic resistances. Cells with \c porosityF >= 1 (pure gas)
    are skipped.

    The Forchheimer term matters for high-velocity flow through coarse
    beds. For slow flow inside a fine porous medium the Darcy term
    alone is usually enough — leave \c porosityProperties absent.

SourceFiles
    fieldPorosityModel.H
    fieldPorosityModel.C
    fieldPorosityModelTemplates.C

\*---------------------------------------------------------------------------*/

#ifndef fieldPorosityModel_H
#define fieldPorosityModel_H

#include "fvMesh.H"
#include "dictionary.H"
#include "fvMatricesFwd.H"
#include "runTimeSelectionTables.H"
#include "coordinateSystem.H"
#include "dimensionedVector.H"
#include "keyType.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

/*---------------------------------------------------------------------------*\
                        Class porosityModel Declaration
\*---------------------------------------------------------------------------*/

class fieldPorosityModel
:
    public regIOobject
{
    // Private Member Functions

        //- Disallow default bitwise copy construction
        fieldPorosityModel(const fieldPorosityModel&) = delete;

        //- Disallow default bitwise assignment
        void operator=(const fieldPorosityModel&) = delete;


protected:

    // Protected data

        //- Porosity name
        word name_;

        //- Reference to the mesh database
        const fvMesh& mesh_;

        //- Forchheimer coefficient
        scalar f_;

        //- porosity Field
        volScalarField& porosityF_;

    // Protected Member Functions

        //- Df based viscous and intertial resistance
        template<class RhoFieldType>
        void addViscousInertialResistance
        (
            scalarField& Udiag,
            vectorField& Usource,
            const labelList& cells,
            const scalarField& V,
            const RhoFieldType& rho,
            const scalarField& mu,
            const vectorField& U,
            tensorField& Df
        ) const;

        //- Return label index
        label fieldIndex(const label index) const;


public:

    //- Runtime type information
    TypeName("fieldPorosityModel");

    //- Selection table
    declareRunTimeSelectionTable
    (
        autoPtr,
        fieldPorosityModel,
        mesh,
        (
            const fvMesh& mesh,
    	    volScalarField& porosityF
        ),
        (mesh, porosityF)
    );

    //- Constructor
    fieldPorosityModel
    (
        const fvMesh& mesh,
        volScalarField& porosityF
    );


    //- Selector
    static autoPtr<fieldPorosityModel> New
    (
        const fvMesh& mesh,
        volScalarField& porosityF
    );

    //- Destructor
    virtual ~fieldPorosityModel();


    // Member Functions

        //- Return const access to the porosity model name
        inline const word& name() const;

        //- Return const access to the porosity active flag
        inline bool active() const;

        //- Transform the model data wrt mesh changes
        void transformModelData();

        //- Add resistance based on Df field
        void addResistance(fvVectorMatrix& UEqn, volTensorField& Df) const;

    // I-O

        //- Write
        bool writeData(Ostream& os) const;

        //- Inherit read from regIOobject
        using regIOobject::read;

        //- Read porosity dictionary
        bool read(const dictionary& dict);
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //
#ifdef NoRepository
#include "fieldPorosityModelTemplates.C"
#endif

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
