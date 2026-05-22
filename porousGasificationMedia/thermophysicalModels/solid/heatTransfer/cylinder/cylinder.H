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
    Foam::cylinderCONV

Description
    Interphase convective heat transfer for flow over a regular
    lattice of parallel solid cylinders.

    Geometry assumption: the solid phase is a constant number of
    parallel cylinders. Given the initial porosity
    \f$\varepsilon_0\f$ and initial cylinder radius \f$a_{c0}\f$, the
    current radius \f$a_c(t)\f$ evolves with porosity (the cylinder
    shrinks as the bed converts):
    \f$a_c = a_{c0}\sqrt{(1-\varepsilon)/(1-\varepsilon_0)}\f$. The
    SAV is \f$SAV = 2(1-\varepsilon)/a_c\f$.

    Heat-transfer coefficient uses a Nu(Re, Pr) correlation for
    cross-flow over a cylinder bundle. Re, Pr and Nu are evaluated
    per cell from the local gas state (\c Up_, \c rhop_, \c mup_,
    \c alphap_).

    TODO: walk through with the user — confirm the Nu correlation in
    cylinder.C (Zukauskas? Churchill-Bernstein?) against the intended
    reference.

SourceFiles
    cylinder.H
    cylinder.C

\*---------------------------------------------------------------------------*/

#ifndef cylinder_H
#define cylinder_H

#include "heatTransferModel.H"
#include "fluidThermo.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

/*---------------------------------------------------------------------------*\
                           Class const Declaration
\*---------------------------------------------------------------------------*/



class cylinderCONV
:
    public heatTransferModel
{
private:

    scalar cylinderRadius_;

    //- Velocity.
    const volVectorField& Up_;

    //- Gas density
    const volScalarField& rhop_;

    //- Thermal diffusivity [kg/m/s]
    const volScalarField& alphap_;

    //- Dynamic viscosity [kg/m/s]
    const volScalarField& mup_;

    //- Reference to the fundamental fluid thermodynamic properties
    const fluidThermo& thermop_;


    // Private member functions

        //- Reynolds number
        scalar Re(const label cellI) const;

        //- Prandtl number
        scalar Pr(const label cellI) const;

        //- Nusselt number
        scalar Nu(const label cellI) const;

        //- Gas thermal conductivity
        scalar kf(const label cellI) const;

public:

    //- Runtime type information
    TypeName("cylinderCONV");


    // Constructors

        //- Construct from porosity and initial porosity.
        cylinderCONV
        (
            const volScalarField& porosity,
            const volScalarField& initialPorosity
        );

    // Selectors

        //- Return a reference to the selected heatTransfer model
        static autoPtr<cylinderCONV> New
        (
            const volScalarField& porosity,
            const volScalarField& initialPorosity
        );

    //- Destructor
    virtual ~cylinderCONV()
    {}


    // Member Functions

        //- Return the field for heat transfer between solid/gas.
        virtual tmp<volScalarField> CONV() const;

        //- Read heatTransferProperties dictionary
        virtual bool read();

};
// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
