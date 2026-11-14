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
    One DEM particle this rank located in its mesh at the last exchange: the
    backend-neutral view lambdaDotModel reads, filled either by pgfYadeCoupler
    or, on the legacy path, from FoamYade's found particles.

\*---------------------------------------------------------------------------*/

#ifndef demParticle_H
#define demParticle_H

#include "DynamicList.H"
#include "vector.H"

namespace Foam
{

struct demParticle
{
    label id;         // Yade body id (legacy path: index in its sender's buffer)
    label cell;       // cell holding the particle centre
    vector U;         // particle velocity [m/s]
    scalar lambda;    // Yade's State::lambda_
};

typedef DynamicList<demParticle> demParticleList;

} // namespace Foam

#endif
