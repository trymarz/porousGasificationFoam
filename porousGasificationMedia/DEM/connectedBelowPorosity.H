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

Description
    The porosity below which the solid is connected, read the way
    volPyrolysis's SolidState reads it: 1 - solidRegimes/
    connectedAboveSolidFraction in constant/pyrolysisProperties. Required
    here, as criticalPorosity was; the old key is refused with its
    replacement.

\*---------------------------------------------------------------------------*/

#ifndef connectedBelowPorosity_H
#define connectedBelowPorosity_H

#include "dictionary.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

//- Porosity below which the solid is connected, from pyrolysisCoeffs
inline scalar connectedBelowPorosity(const dictionary& pyrolysisCoeffs)
{
    if (pyrolysisCoeffs.found("criticalPorosity"))
    {
        FatalIOErrorInFunction(pyrolysisCoeffs)
            << "criticalPorosity is no longer read. Replace it with" << nl
            << "    solidRegimes { connectedAboveSolidFraction "
            << 1.0 - pyrolysisCoeffs.get<scalar>("criticalPorosity")
            << "; }" << nl
            << "The threshold is now a solid volume fraction, 1 - porosity."
            << exit(FatalIOError);
    }

    return
        1.0
      - pyrolysisCoeffs.subDict("solidRegimes")
            .get<scalar>("connectedAboveSolidFraction");
}

} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
