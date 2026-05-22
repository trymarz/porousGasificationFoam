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
    Template implementation of \c solidOde.
    See the class Description in the .H.

    \c solve() packs the per-cell state into the flat vector
    \c cTp_ = [c_0, ..., c_{nSpecie-1}, T, p] and drives the
    registered \c ODESolver between t0 and t0+dt. The solver is
    selected by \c solidOdeCoeffs/solver in
    \c constant/chemistryProperties (e.g. \c RKF45, \c RKCK45).
    Concentrations are clipped to zero after unpacking to protect
    downstream consumers against integrator overshoots near zero.

\*---------------------------------------------------------------------------*/

#include "solidOde.H"

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

template<class ChemistryModel>
Foam::solidOde<ChemistryModel>::solidOde
(
    const HGSSolidThermo& thermo,
    PtrList<volScalarField>& gasPhaseGases
)
:
    solidChemistrySolver<ChemistryModel>(thermo, gasPhaseGases),
    coeffsDict_(this->subDict("solidOdeCoeffs")),
    odeSolver_(ODESolver::New(*this, coeffsDict_)),
    cTp_(this->nEqns())
{}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

template<class ChemistryModel>
Foam::solidOde<ChemistryModel>::~solidOde()
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //
template<class ChemistryModel>
//- Integrate the per-cell chemistry ODE over [t0, t0+dt].
//
//  Packs the integration state as cTp_ = [c_0, ..., c_{nSpecie-1}, T,
//  p] and drives the registered ODESolver between t0 and t0+dt. The
//  integrator updates dtEst, which is returned as the next chemistry
//  time step. Concentrations are clipped at zero on unpacking to
//  protect downstream consumers against integrator overshoots.
Foam::scalar Foam::solidOde<ChemistryModel>::solve
(
    scalarField& c,
    const scalar T,
    const scalar p,
    const scalar t0,
    const scalar dt
) const
{
    // Resize the ODE state vector if the underlying solver reduced
    // the active mechanism (rarely relevant for the heterogeneous
    // case but kept for parity with OpenFOAM's gas-phase chemistry
    // solver path).
    if (odeSolver_->resize())
    {
        odeSolver_->resizeField(cTp_);
    }

    const label nSpecie = this->nSpecie();

    // Copy the concentration,T and P to the total solve-vector
    for (int i=0; i<nSpecie; i++)
    {
        cTp_[i] = c[i];
    }
    cTp_[nSpecie] = T;
    cTp_[nSpecie+1] = p;

    scalar dtEst = dt;

    odeSolver_->solve
    (
        t0,
        t0 + dt,
        cTp_,
        dtEst
    );
    for (int i=0; i<nSpecie; i++)
    {
        c[i] = max(0.0, cTp_[i]);
    }

    return dtEst;
}


// ************************************************************************* //
