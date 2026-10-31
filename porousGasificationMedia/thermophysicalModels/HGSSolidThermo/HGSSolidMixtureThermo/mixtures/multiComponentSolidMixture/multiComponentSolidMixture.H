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

        //- Mass fractions of a cell
        class cellY
        {
            const PtrList<volScalarField>& Y_;
            const label celli_;

        public:

            cellY(const PtrList<volScalarField>& Y, label celli)
            :
                Y_(Y), celli_(celli)
            {}

            scalar operator[](const label i) const
            {
                return Y_[i][celli_];
            }
        };

        //- Mass fractions a patch face prescribes, normalised by their sum
        class patchFaceY
        {
            const PtrList<volScalarField>& Y_;
            const label patchi_;
            const label facei_;
            const scalar sum_;

        public:

            patchFaceY
            (
                const PtrList<volScalarField>& Y,
                label patchi,
                label facei,
                scalar sum
            )
            :
                Y_(Y), patchi_(patchi), facei_(facei), sum_(sum)
            {}

            scalar operator[](const label i) const
            {
                return Y_[i].boundaryField()[patchi_][facei_]/sum_;
            }
        };

        //- True where every Y_i is fixedValue on patch patchi
        bool prescribesComposition(label patchi) const;

        //- property(T, Y) on patch patchi: of the patch's own composition
        //  where it prescribes one, else of the face cell's
        template<class Property>
        tmp<scalarField> patchProperty
        (
            const scalarField& Tp,
            label patchi,
            const Property& property
        ) const;

        //- Return molar fraction for component i of composition Y at T
        template<class Composition>
        scalar X(label i, const Composition& Y, scalar T) const;

        // Properties of composition Y (cellY or patchFaceY)
        template<class Composition>
        scalar rhoOf(scalar T, const Composition& Y) const;
        template<class Composition>
        scalar kappaOf(scalar T, const Composition& Y) const;
        template<class Composition>
        scalar sigmaSOf(scalar T, const Composition& Y) const;
        template<class Composition>
        scalar KOf(scalar T, const Composition& Y) const;
        template<class Composition>
        scalar emissivityOf(scalar T, const Composition& Y) const;
        template<class Composition>
        scalar hfOf(const Composition& Y) const;
        template<class Composition>
        scalar hsOf(scalar T, const Composition& Y) const;
        template<class Composition>
        scalar hOf(scalar T, const Composition& Y) const;
        template<class Composition>
        scalar CpOf(scalar T, const Composition& Y) const;


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


        // Patch values at patch temperatures Tp, of the composition
        // patchProperty() selects

            tmp<scalarField> rho(const scalarField& Tp, label patchi) const;
            tmp<scalarField> kappa(const scalarField& Tp, label patchi) const;
            tmp<scalarField> sigmaS(const scalarField& Tp, label patchi) const;
            tmp<scalarField> K(const scalarField& Tp, label patchi) const;
            tmp<scalarField> emissivity
            (
                const scalarField& Tp,
                label patchi
            ) const;
            tmp<scalarField> hf(const scalarField& Tp, label patchi) const;
            tmp<scalarField> hs(const scalarField& Tp, label patchi) const;
            tmp<scalarField> Cp(const scalarField& Tp, label patchi) const;
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
