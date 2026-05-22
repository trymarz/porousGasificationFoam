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
    Foam::HGSSolidMixtureThermo

Description
    Concrete \c HGSSolidThermo templated on a multi-component mixture.

    The MixtureType template parameter is one of:
      - \c multiComponentSolidMixture<...>     — non-reacting mixture;
      - \c reactingSolidMixture<...>           — gas-only side
                                                 reactions (S = S + G);
      - \c reactingSolidHeterogeneousMixture<...> — heterogeneous
                                                 reactions (S + G = S + G).

    Owns the cached cell-wise thermal conductivity field \c K_, which
    is recomputed by \c calculate() in \c correct(). \c Cp(), \c Hf()
    and \c hs() are produced on demand from the current temperature
    and composition. Per-patch overloads iterate over a patch's
    cells/faces.

    Multiple inheritance from both \c HGSSolidThermo (the abstract
    base) and MixtureType lets \c composition() simply return *this
    typed as MixtureType, which is what callers downstream
    (ODESolidHeterogeneousChemistryModel) cast into.

    Concrete instantiations are produced by \c makeHGSSolidMixtureThermo.H
    macros against the \c forCommonSolids.H combination matrix; the
    name string of the resulting type is the brackets-and-commas
    composition expected in \c solidThermoType in chemistryProperties
    (e.g. \c const<constRad<constThermo<constRho>>>).

SourceFiles
    HGSSolidMixtureThermo.H
    HGSSolidMixtureThermo.C
    HGSSolidMixtureThermos.C
    makeHGSSolidMixtureThermo.H

\*---------------------------------------------------------------------------*/

#ifndef HGSSolidMixtureThermo_H
#define HGSSolidMixtureThermo_H

#include "HGSSolidThermo.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

/*---------------------------------------------------------------------------*\
                    Class HGSSolidMixtureThermo Declaration
\*---------------------------------------------------------------------------*/

template<class MixtureType>
class HGSSolidMixtureThermo
:
    public HGSSolidThermo,
    public MixtureType
{
protected:

    // Protected data

        //- Thermal conductivity [W/m/K]
        volScalarField K_;


private:

    // Private Member Functions

        //- Calculate K
        void calculate();


public:

    //- Runtime type information
    TypeName("HGSSolidMixtureThermo");


    // Constructors

        //- Construct from mesh
        HGSSolidMixtureThermo(const fvMesh&);

        //- Construct from mesh and dictionary
        HGSSolidMixtureThermo(const fvMesh&, const dictionary&);

        //- Construct from mesh and dictionary
        HGSSolidMixtureThermo(const fvMesh&, const dictionary&, const PtrList<volScalarField>& gasPhaseGases);


    //- Destructor
    virtual ~HGSSolidMixtureThermo();


    // Member functions

        //- Return the compostion of the solid mixture
        virtual MixtureType& composition()
        {
            return *this;
        }

        //- Return the compostion of the solid mixture
        virtual const MixtureType& composition() const
        {
            return *this;
        }

        //- Update properties
        virtual void correct();

        // Access functions

            //- Thermal conductivity [W/m/K]
            virtual const volScalarField& K() const;


            // Derived properties

                //- Specific heat capacity [J/(kg.K)]
                virtual tmp<volScalarField> Cp() const;

                //- Heat of formation [J/kg]
                virtual tmp<volScalarField> Hf() const;

                //- Sensible enthalpy [J/(kg.K)]
                virtual tmp<volScalarField> hs() const;


        // Patches variables


            //- Density [kg/m3]
            virtual tmp<scalarField> rho(const label patchI) const;

            //- Specific heat capacity [J/(kg.K)]
            virtual tmp<scalarField> Cp(const label patchI) const;

            //- Sensible enthalpy [J/(kg.K)]
            virtual tmp<scalarField> hs(const label patchI) const;

            //- Thermal conductivity [W/(m.K)]
            virtual tmp<scalarField> K(const label patchI) const;

            //- Heat of formation [J/kg]
            virtual tmp<scalarField> Hf(const label patchI) const;

            //- Scatter coefficient [1/m]
            virtual tmp<scalarField> sigmaS(const label patchI) const;

            //- Absorptivity [1/m]
            virtual tmp<scalarField> kappa(const label patchI) const;

            //- Emissivity []
            virtual tmp<scalarField> emissivity(const label patchI) const;


        //- Read thermophysicalProperties dictionary
        virtual bool read();

        //- Write the HGSSolidThermo properties
        virtual bool writeData(Ostream& os) const;
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#ifdef NoRepository
#   include "HGSSolidMixtureThermo.C"
#endif

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
