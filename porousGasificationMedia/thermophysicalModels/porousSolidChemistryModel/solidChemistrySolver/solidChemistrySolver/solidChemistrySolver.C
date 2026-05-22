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

Description
    Constructor/destructor of the abstract \c solidChemistrySolver. The
    body is intentionally empty — all state lives in the wrapped
    ChemistryModel base. Derived solvers (solidOde) add their own
    integrator state.

\*---------------------------------------------------------------------------*/

#include "solidChemistrySolver.H"

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

template<class ChemistryModel>
Foam::solidChemistrySolver<ChemistryModel>::solidChemistrySolver
(
    const HGSSolidThermo& thermo,
    PtrList<volScalarField>& gasPhaseGases
)
:
    ChemistryModel(thermo, gasPhaseGases)
{}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

template<class ChemistryModel>
Foam::solidChemistrySolver<ChemistryModel>::~solidChemistrySolver()
{}


// ************************************************************************* //
