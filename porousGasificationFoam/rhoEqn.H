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
    rhoEqn

Description
    Gas-phase continuity equation in a porous medium.

    Solves
    \f[
        \frac{\partial (\varphi\, \rho)}{\partial t}
      + \nabla \cdot \phi
      \;=\;
        \dot{m}_{s\rightarrow g}\, (1 - \varphi)
    \f]

    where:
      - \f$\varphi\f$ is the porosity field (\c porosityF), so the
        time-derivative carries the gas-occupied fraction of each cell,
      - \f$\phi\f$ is the compressible mass flux (already a per-cell
        total quantity),
      - the source is the total solid-to-gas mass production rate per
        unit total cell volume, written as the per-solid-volume rate
        \c Srho times the solid fraction \c (1 - porosityF). \c Srho is
        the sum over heterogeneous reactions of mass leaving the solid;
        in YEqn.H the per-species form \c pyrolysisZone.Srho(i) carries
        the same source split among gas species.

\*---------------------------------------------------------------------------*/

// Tag eqZx2uHGn042 — paper cross-reference.

// Refresh the cell-wise solid-to-gas mass production from the pyrolysis
// model. This was computed in pyrolysisZone.evolve() earlier in the
// time step; here we just publish it as the source for the gas
// continuity and (further down) for the pressure equation.
Srho = pyrolysisZone.Srho();

{
    fvScalarMatrix rhoEqn
    (
        fvm::ddt(porosityF, rho)
      + fvc::div(phi)
      ==
        Srho * (1.0 - porosityF)
    );

    fvOptions.constrain(rhoEqn);

    rhoEqn.solve();

    fvOptions.correct(rho);
}

// ************************************************************************* //
