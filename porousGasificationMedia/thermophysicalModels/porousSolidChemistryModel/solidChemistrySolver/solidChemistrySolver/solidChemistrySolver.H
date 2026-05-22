/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     | Website:  https://openfoam.org
    \\  /    A nd           | Copyright (C) 2011-2019 OpenFOAM Foundation
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
    Foam::solidChemistrySolver

Description
    Abstract per-cell ODE solver wrapper around a chemistry model.

    Template-parameterised on the chemistry model (typically
    \c ODESolidHeterogeneousChemistryModel<...>). Adds the pure-virtual
    \c solve(c, T, p, t0, dt) entry point that returns the chemical
    time scale after a single-cell integration. Concrete derivatives:
      - \c solidOde — uses an OpenFOAM \c ODESolver (e.g. \c seulex)
        configured via \c solidOdeCoeffs in chemistryProperties.

    The base merely forwards construction to its ChemistryModel base;
    the derivation pattern lets a future implementation pick a
    non-ODE solver (analytic, EXPRB, ...) without churning the
    chemistry model layer above.

SourceFiles
    solidChemistrySolver.H
    solidChemistrySolver.C
    solidChemistrySolvers.C

\*---------------------------------------------------------------------------*/

#ifndef solidChemistrySolver_H
#define solidChemistrySolver_H

#include "IOdictionary.H"
#include "scalarField.H"
#include "volFieldsFwd.H"
#include "HGSSolidThermo.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

/*---------------------------------------------------------------------------*\
                      Class solidChemistrySolver Declaration
\*---------------------------------------------------------------------------*/

template<class ChemistryModel>
class solidChemistrySolver
:
    public ChemistryModel
{

public:

    // Constructors

        //- Construct from thermo
        solidChemistrySolver
        (
            const HGSSolidThermo& thermo,
            PtrList<volScalarField>& gasPhaseGases
        );


    //- Destructor
    virtual ~solidChemistrySolver();


    // Member Functions

        //- Update the concentrations and return the chemical time
        virtual scalar solve
        (
            scalarField &c,
            const scalar T,
            const scalar p,
            const scalar t0,
            const scalar dt
        ) const = 0;
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#ifdef NoRepository
    #include "solidChemistrySolver.C"
#endif

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
