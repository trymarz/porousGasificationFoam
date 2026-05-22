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
    Foam::solidOde

Description
    ODE-based per-cell chemistry solver.

    Wraps an OpenFOAM \c ODESolver (e.g. \c seulex, \c rodas23,
    \c rodas34 — selected via the \c solver entry in
    \c solidOdeCoeffs in chemistryProperties) around the chemistry
    model's ODESystem interface. The integration state vector is
    \c cTp_ = [c_0, c_1, ..., c_{nSpecie-1}, T, p].

    \c solve(c, T, p, t0, dt) packs c, T, p into cTp_, drives the
    integrator from t0 to t0+dt, unpacks back into c (clipped at zero
    to avoid negative concentrations), and returns the integrator's
    suggested next step.

    Used by \c volPyrolysis::evolveRegion via
    \c porousThermoSolidChemistryModel::solve() which delegates to
    this class.

SourceFiles
    solidOde.H
    solidOde.C

\*---------------------------------------------------------------------------*/

#ifndef solidOde_H
#define solidOde_H

#include "solidChemistrySolver.H"
#include "ODESolver.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

/*---------------------------------------------------------------------------*\
                            Class solidOde Declaration
\*---------------------------------------------------------------------------*/

template<class ChemistryModel>
class solidOde
:
    public solidChemistrySolver<ChemistryModel>
{
    // Private Data

        dictionary coeffsDict_;
        const word solverName_;
        mutable autoPtr<ODESolver> odeSolver_;

        // Model constants
        mutable scalarField cTp_;

public:

    //- Runtime type information
    TypeName("solidOde");


    // Constructors

        //- Construct from thermo
        solidOde
        (
            const HGSSolidThermo& thermo,
            PtrList<volScalarField>& gasPhaseGases
        );


    //- Destructor
    virtual ~solidOde();


    // Member Functions

        //- Update the concentrations and return the chemical time
        virtual scalar solve
        (
            scalarField &c,
            const scalar T,
            const scalar p,
            const scalar t0,
            const scalar dt
        ) const;
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#ifdef NoRepository
    #include "solidOde.C"
#endif

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
