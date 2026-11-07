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
    cTp_(this->nEqns()),
    dcTpdt_(this->nEqns()),
    maxSteps_(coeffsDict_.getOrDefault<label>("maxSteps", 10000))
{
    WarningInFunction
        << "The solid ODE integration ends an interval as soon as all "
        << "reaction rates are exactly zero." << nl
        << "    Stock seulex rejects zero-error steps until dx underflows "
        << "(#200)." << endl;
}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

template<class ChemistryModel>
Foam::solidOde<ChemistryModel>::~solidOde()
{}


// * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * * //

template<class ChemistryModel>
bool Foam::solidOde<ChemistryModel>::ratesAreZero(const scalar x) const
{
    this->derivatives(x, cTp_, dcTpdt_);

    forAll(dcTpdt_, i)
    {
        if (dcTpdt_[i] != 0)
        {
            return false;
        }
    }

    return true;
}


template<class ChemistryModel>
void Foam::solidOde<ChemistryModel>::integrate
(
    const scalar xStart,
    const scalar xEnd,
    scalar& dxTry
) const
{
    // ODESolver::solve(xStart, xEnd, y, dxEst) plus the zero-rate exit.
    ODESolver::stepState step(dxTry);
    scalar x = xStart;

    for (label nStep=0; nStep<maxSteps_; ++nStep)
    {
        // The first step always accepts zero error and sets the solver's next
        // step, which idle cells need to grow. The exit keeps the last dxTry:
        // the state has not changed, so there is nothing new to estimate from.
        if (!step.first && ratesAreZero(x))
        {
            dxTry = step.dxTry;
            return;
        }

        const scalar dxTry0 = step.dxTry;

        step.reject = false;

        if ((x + step.dxTry - xEnd)*(x + step.dxTry - xStart) > 0)
        {
            step.last = true;
            step.dxTry = xEnd - x;
        }

        odeSolver_->solve(x, cTp_, step);

        if ((x - xEnd)*(xEnd - xStart) >= 0)
        {
            if (nStep > 0 && step.last)
            {
                step.dxTry = dxTry0;
            }

            dxTry = step.dxTry;

            return;
        }

        step.first = false;

        if (step.reject)
        {
            step.prevReject = true;
        }
    }

    FatalErrorInFunction
        << "Integration steps greater than maximum " << maxSteps_ << nl
        << "    xStart = " << xStart << ", xEnd = " << xEnd
        << ", x = " << x << ", dxDid = " << step.dxDid << nl
        << "    y = " << cTp_
        << exit(FatalError);
}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //
template<class ChemistryModel>
Foam::scalar Foam::solidOde<ChemistryModel>::solve
(
    scalarField& c,
    const scalar T,
    const scalar p,
    const scalar t0,
    const scalar dt
) const
{

    // Reset the size of the ODE system to the simplified size when mechanism
    // reduction is active
    if (odeSolver_->resize())
    {
        odeSolver_->resizeField(cTp_);
        odeSolver_->resizeField(dcTpdt_);
    }

    const label nSpecie = this->nSpecie();

    // Copy the concentration,T and P to the total solve-vector
    for (int i=0; i<nSpecie; i++)
    {
        cTp_[i] = c[i];
    }
    cTp_[nSpecie] = T;
    cTp_[nSpecie+1] = p;

    this->startHeatIntegration(cTp_);

    scalar dtEst = dt;

    integrate(t0, t0 + dt, dtEst);

    this->finishHeatIntegration(cTp_);

    for (int i=0; i<nSpecie; i++)
    {
        c[i] = max(0.0, cTp_[i]);
    }

    return dtEst;
}


// ************************************************************************* //
