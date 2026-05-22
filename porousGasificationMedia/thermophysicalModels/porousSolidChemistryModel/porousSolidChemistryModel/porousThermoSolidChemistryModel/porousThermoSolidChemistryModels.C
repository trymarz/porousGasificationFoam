/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     | Website:  https://openfoam.org
    \\  /    A nd           | Copyright (C) 2011-2020 OpenFOAM Foundation
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
    Instantiation point for the \c porousThermoSolidChemistryModel
    template against the concrete \c HGSSolidThermo. Brings the
    runtime selection table into existence so derived classes (the
    solidOde + ODESolidHeterogeneousChemistryModel combinations
    instantiated in \c solidChemistrySolvers.C) can register
    themselves at static-init time.

\*---------------------------------------------------------------------------*/

#include "porousThermoSolidChemistryModel.H"

#include "HGSSolidThermo.H"

#include "makeChemistryModel.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
    defineChemistryModel(porousThermoSolidChemistryModel, HGSSolidThermo);
}

// ************************************************************************* //
