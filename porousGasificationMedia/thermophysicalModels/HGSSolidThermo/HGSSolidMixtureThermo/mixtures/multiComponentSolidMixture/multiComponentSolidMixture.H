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
    Foam::multiComponentSolidMixture

Description
    Foam::multiComponentSolidMixture

\*---------------------------------------------------------------------------*/

#ifndef multiComponentSolidMixture_H
#define multiComponentSolidMixture_H

#include "PtrList.H"
#include "autoPtr.H"
#include "basicSolidMixture.H"


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

/*---------------------------------------------------------------------------*\
                 Class multiComponentSolidMixture Declaration
\*---------------------------------------------------------------------------*/

template<class ThermoSolidType>
class multiComponentSolidMixture
:
    public basicSolidMixture
{

    // Private data

        //- Solid data
        PtrList<ThermoSolidType> solidData_;


    // Private Member Functions

        //- Correct the mass fractions to sum to 1
        void correctMassFractions();

        //- Mass fractions of a cell, or of a patch face normalised by sum
        class massFractions
        {
            const PtrList<volScalarField>& Y_;
            const label celli_;
            const label patchi_;
            const label facei_;
            const scalar sum_;

        public:

            massFractions(const PtrList<volScalarField>& Y, label celli)
            :
                Y_(Y), celli_(celli), patchi_(-1), facei_(-1), sum_(1)
            {}

            massFractions
            (
                const PtrList<volScalarField>& Y,
                label patchi,
                label facei,
                scalar sum
            )
            :
                Y_(Y), celli_(-1), patchi_(patchi), facei_(facei), sum_(sum)
            {}

            scalar operator[](const label i) const
            {
                return patchi_ < 0
                  ? Y_[i][celli_]
                  : Y_[i].boundaryField()[patchi_][facei_]/sum_;
            }
        };

        //- Composition at face facei of patch patchi: the patch's own where
        //  every Y_i fixes its value there, else the face cell's
        massFractions faceY(label patchi, label facei) const;

        //- Return molar fraction for component i of composition Y at T
        scalar X(label i, const massFractions& Y, scalar T) const;

        // Properties of composition Y
        scalar rho(scalar T, const massFractions& Y) const;
        scalar kappa(scalar T, const massFractions& Y) const;
        scalar sigmaS(scalar T, const massFractions& Y) const;
        scalar K(scalar T, const massFractions& Y) const;
        scalar emissivity(scalar T, const massFractions& Y) const;
        scalar hf(const massFractions& Y) const;
        scalar hs(scalar T, const massFractions& Y) const;
        scalar h(scalar T, const massFractions& Y) const;
        scalar Cp(scalar T, const massFractions& Y) const;


public:


    // Constructors

        //- Construct from dictionary and mesh
        multiComponentSolidMixture(const dictionary&, const fvMesh&);


    //- Destructor
    virtual ~multiComponentSolidMixture()
    {}


    // Member Functions

        //- Return the raw solid data
        const PtrList<ThermoSolidType>& solidData() const
        {
            return solidData_;
        }

        //- Read dictionary
        void read(const dictionary&);


        // Cell-based properties

            //- Density
            virtual scalar rho(scalar T, label celli) const;
            //virtual scalar rhoT(scalar T, label celli);

            //- Absorption coefficient
            virtual scalar kappa(scalar T, label celli) const;

            //- Scatter coefficient
            virtual scalar sigmaS(scalar T, label celli) const;

            //- Thermal conductivity
            virtual scalar K(scalar T, label celli) const;

            //- Emissivity coefficient
            virtual scalar emissivity(scalar T, label celli) const;

            //- Formation enthalpy
            virtual scalar hf(scalar T, label celli) const;

            //- Sensible enthalpy
            virtual scalar hs(scalar T, label celli) const;

            //- Total enthalpy
            virtual scalar h(scalar T, label celli) const;

            //- Specific heat capacity
            virtual scalar Cp(scalar T, label celli) const;


        // Patch-face properties, of the composition faceY() selects

            scalar rho(scalar T, label patchi, label facei) const;
            scalar kappa(scalar T, label patchi, label facei) const;
            scalar sigmaS(scalar T, label patchi, label facei) const;
            scalar K(scalar T, label patchi, label facei) const;
            scalar emissivity(scalar T, label patchi, label facei) const;
            scalar hf(scalar T, label patchi, label facei) const;
            scalar hs(scalar T, label patchi, label facei) const;
            scalar Cp(scalar T, label patchi, label facei) const;
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#ifdef NoRepository
#   include "multiComponentSolidMixture.C"
#endif

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
