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
    Solve the continuity for density.

\*---------------------------------------------------------------------------*/
// eqZx2uHGn042

Srho = pyrolysisZone.Srho();

{
    // The gas continuity is a diagonal-only system (the only implicit term is
    // the ddt; div(phi) and Srho are explicit), so OpenFOAM solves it with the
    // `diagonal` solver: rho[i] = source[i] / diag[i].  The diagonal here is
    // porosityF*V/deltaT.  volPyrolysis::evolvePorosity() floors the gas void
    // fraction to exactly 0 in solid-full cells, which makes that diagonal
    // exactly zero -> division by zero -> SIGFPE in diagonalSolver::solve.
    // Floor porosityF in the ddt term to a small positive value so the diagonal
    // stays non-zero; this also pins rho to ~its previous value in those cells
    // (gas density is physically irrelevant where there is no gas) rather than
    // letting source/SMALL blow it up.  The explicit (1 - porosityF) source is
    // left on the true field.
    const dimensionedScalar porFloor("porFloor", dimless, 1e-3);

    fvScalarMatrix rhoEqn
    (
        fvm::ddt(max(porosityF, porFloor), rho)
      + fvc::div(phi)
      ==
        Srho * (1.0 - porosityF)
    );

    fvOptions.constrain(rhoEqn);

    rhoEqn.solve();

    fvOptions.correct(rho);
}

// ************************************************************************* //
