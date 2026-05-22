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
    Foam::radiationModels::heterogeneousP1

Description
    P1 (first-moment) radiation model for the gas + solid mixture.

    Solves the diffusion equation for the incident radiation field
    \f$G\f$ [W/m^2]:
    \f[
        \nabla \cdot (\gamma\, \nabla G) - (a + a_s\,\mathrm{whereIs}
            + a_b\,\mathrm{surfF})\, G
        = -4 \left[
              a\, \sigma\, T_g^4
            + (a_s\,\mathrm{whereIs} + a_b\,\mathrm{surfF})\,
              \sigma\, T_s^4
          \right]
    \f]
    with diffusion coefficient
    \f[
        \gamma = \frac{1}{3(a + a_s + a_b) + \sigma_{eff}}.
    \f]

    Coefficients per cell:
      - \f$a\f$ — gas-phase absorption [1/m] (from absorptionEmission
        model's \c aCont);
      - \f$a_s\f$ — solid-phase volumetric absorption [1/m]
        (\c asCont), masked by \c whereIs;
      - \f$a_b\f$ — solid surface absorption [1/m] (\c borderAsCont),
        applied only on cells with \c surfF != 0;
      - \f$\sigma_{eff}\f$ — effective scattering coefficient from
        the scatter model.

    The solid radiative source delivered to the energy equation is
    \f[
        S_{rad} =
        \begin{cases}
          (a_s\, G - 4\, a_s\, \sigma\, T_s^4)\,\mathrm{whereIs} &
            \text{interior solid cells}\\
          \left[(a_s + \mathrm{surfF}\, a_b)\, G
                - 4(a_s + \mathrm{surfF}\, a_b)\, \sigma\, T_s^4\right]
                \,\mathrm{whereIs} & \text{surface cells}
        \end{cases}
    \f]
    so the source switches in the surface term \f$a_b\f$ on the bed
    edge only. \c borderL_ is the user-supplied surface-layer
    thickness scaling \c surfF.

    The boundary radiative heat flux \c qr_ is written to processor
    patches via \f$-\gamma\, \partial G/\partial n\f$ for diagnostics.

    Equation tags retained: eqZx2uHGn009 (G transport),
    eqZx2uHGn010 (gamma), eqZx2uHGn012 (Sh), eqZx2uHGn013
    (solidRadiation).

SourceFiles
    heterogeneousP1.H
    heterogeneousP1.C

\*---------------------------------------------------------------------------*/

#ifndef heterogeneousRadiationModelheterogeneousP1_H
#define heterogeneousRadiationModelheterogeneousP1_H

#include "heterogeneousRadiationModel.H"
#include "volFields.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace radiationModels
{

/*---------------------------------------------------------------------------*\
                           Class heterogeneousP1 Declaration
\*---------------------------------------------------------------------------*/

class heterogeneousP1
:
    public heterogeneousRadiationModel
{
    // Private data

        //- Incident radiation / [W/m2]
        volScalarField G_;

        //- Total radiative heat flux [W/m2]
        volScalarField qr_;

        //- Absorption coefficient
        volScalarField a_;

        //- Absorption coefficient
        volScalarField as_;

        //- Absorption coefficient
        volScalarField borderAs_;

        //- Emission contribution
        volScalarField E_;

        //- Emission contribution
        dimensionedScalar borderL_;

        //- porosity field
        const volScalarField& porosityF_;

        //- porosity field
        const volScalarField& surfFI_;

        //- porosity field
        volScalarField surfF_;

	    //- field where porosity is
	    volScalarField whereIs_;

	    //- field where is no porosity
	    volScalarField whereIsNot_;

        //- enthalpy source term for solid
	    volScalarField solidSh_;


public:

    //- Runtime type information
    TypeName("heterogeneousP1");


    // Constructors

        //- Construct from components
        heterogeneousP1
        (
            const volScalarField& T,
            const volScalarField& porosityF,
            const volScalarField& surfF,
            const volScalarField& Ts
        );

        //- Disallow default bitwise copy construction
        heterogeneousP1(const heterogeneousP1&) = delete;


    //- Destructor
    virtual ~heterogeneousP1();


    // Member functions

        // Edit

            //- Solve radiation equation(s)
            void calculate();

            //- Read radiation properties dictionary
            bool read();


        // Access

            //- Source term component (for power of T^4)
            virtual tmp<volScalarField> Rp() const;

            //- Source term component (constant)
            virtual tmp<DimensionedField<scalar, volMesh> > Ru() const;

            //- Source term component (constant)
            virtual tmp<volScalarField> solidSh() const;

    // Member Operators

        //- Disallow default bitwise assignment
        void operator=(const heterogeneousP1&) = delete;
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace radiationModels
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
