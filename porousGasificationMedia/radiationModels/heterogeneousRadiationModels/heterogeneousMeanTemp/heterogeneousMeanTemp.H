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
    Foam::radiationModels::heterogeneousMeanTemp

Description
    Surface-only mean-wall-temperature radiation model.

    Bypasses the full P1 diffusion equation. The radiative energy
    impinging on the bed is taken as the Stefan–Boltzmann emission of
    a representative wall temperature
    \f[
        G = \sigma\, T_{wall,mean}^4
    \f]
    where \f$T_{wall,mean}\f$ is the area-weighted mean over all
    patches of type \c wall. The solid radiative source is applied
    only on the bed surface (cells with \c surfF_[cellI] != 0):
    \f[
        S_{rad}(\text{surf}) = 4\, \mathrm{surfF}\,
            (\mathrm{borderAs}\,G - \mathrm{borderAs}\,\sigma\, T_s^4)
    \f]

    Fast (no Laplacian solve) but valid only when the bed is optically
    thin enough that interior cells receive negligible volumetric
    radiation. Use \c heterogeneousP1 for optically thick beds.

    Requires at least one patch of type \c wall in the case; aborts
    otherwise. \c surfF_ — the surface-cell mask scaled by
    \c borderL/dx — is rebuilt from \c surfFI (the binary border mask
    from volPyrolysis) once per call, accounting for mesh
    dimensionality (1D/2D/3D).

SourceFiles
    heterogeneousMeanTemp.H
    heterogeneousMeanTemp.C

\*---------------------------------------------------------------------------*/

#ifndef heterogeneousRadiationModelheterogeneousMeanTemp_H
#define heterogeneousRadiationModelheterogeneousMeanTemp_H

#include "heterogeneousRadiationModel.H"
#include "volFields.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace radiationModels
{

/*---------------------------------------------------------------------------*\
                           Class heterogeneousMeanTemp Declaration
\*---------------------------------------------------------------------------*/

class heterogeneousMeanTemp
:
    public heterogeneousRadiationModel
{
        // Private data

            //- Incident radiation / [W/m2]
            volScalarField G_;

            //- Total radiative heat flux [W/m2]
            volScalarField Qr_;

            //- Absorption coefficient
            volScalarField borderAs_;

            //- Emission contribution
            dimensionedScalar borderL_;

            //- porosity field
            const volScalarField& porosityF_;

            //- porosity field
            const volScalarField& surfFI_;

            //- porosity field
            volScalarField surfF_;

            //- magnitude of G_ field gradient
            volScalarField solidSh_;


public:

    //- Runtime type information
    TypeName("heterogeneousMeanTemp");


    // Constructors

        //- Construct from components
        heterogeneousMeanTemp
        (
            const volScalarField& T,
            const volScalarField& porosityF,
            const volScalarField& surfF,
            const volScalarField& Ts
        );

        //- Disallow default bitwise copy construction
        heterogeneousMeanTemp(const heterogeneousMeanTemp&) = delete;


    //- Destructor
    virtual ~heterogeneousMeanTemp();


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
        void operator=(const heterogeneousMeanTemp&) = delete;
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace radiationModels
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
