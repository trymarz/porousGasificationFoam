/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     | Website:  https://openfoam.org
    \\  /    A nd           | Copyright (C) 2011-2018 OpenFOAM Foundation
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

Global
    setMultiRegionDeltaT

Description
    Pick the next time step so that both the gas Courant number and the
    solid diffusion number stay below their user limits.

    Inputs (already computed earlier in the main loop):
      - \c CoNum: maximum compressible Courant number across the mesh
        (from \c compressibleCourantNo.H).
      - \c DiNum: maximum solid-region diffusion number
        \f$\alpha_s \Delta t / \Delta x^2\f$ (from \c solidRegionDiffusionNo.H,
        computed in \c volPyrolysis::solidRegionDiffNo()).
      - \c maxCo, \c maxDi, \c maxDeltaT: limits read from
        \c system/controlDict.

    Algorithm:
      - Reduce immediately to whatever the limits demand
        (maxDeltaTFluid, maxDeltaTSolid).
      - Cap the per-step growth factor at 1.2 to avoid stiff bouncing.
      - Cap the absolute step at \c maxDeltaT.

    The chemistry timescale is layered on top of this in
    \c updateChemistryTimeStep.H, which may shrink deltaT further.

\*---------------------------------------------------------------------------*/

if (adjustTimeStep)
{
    if (CoNum == -GREAT)
    {
        CoNum = SMALL;
    }

    if (DiNum == -GREAT)
    {
        DiNum = SMALL;
    }

    scalar maxDeltaTFluid = maxCo/(CoNum + SMALL);
    scalar maxDeltaTSolid = maxDi/(DiNum + SMALL);

    const scalar dt0 = runTime.deltaT().value() *
        min
        (
            min
            (
                min(maxDeltaTFluid, 1.0 + 0.1 * maxDeltaTFluid),
                min(maxDeltaTSolid, 1.0 + 0.1 * maxDeltaTSolid)
            ),
            1.2
        );

    runTime.setDeltaT
    (
        min(dt0,maxDeltaT)
    );
}
// ************************************************************************* //
