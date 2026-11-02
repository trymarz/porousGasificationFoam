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

\*---------------------------------------------------------------------------*/

#include "volPyrolysis.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

inline Foam::volScalarField&
Foam::heterogeneousPyrolysisModels::volPyrolysis::rhoConst() const
{
    return rho_;
}

inline const Foam::volScalarField&
Foam::heterogeneousPyrolysisModels::volPyrolysis::T() const
{
    return T_;
}

inline const Foam::tmp<Foam::volScalarField>
Foam::heterogeneousPyrolysisModels::volPyrolysis::Cp() const
{
    return solidThermo_.Cp();
}

inline const Foam::volScalarField&
Foam::heterogeneousPyrolysisModels::volPyrolysis::kappa() const
{
    return solidThermo_.kappa();
}

inline const Foam::volScalarField&
Foam::heterogeneousPyrolysisModels::volPyrolysis::K() const
{
    return solidThermo_.K();
}

inline const Foam::volScalarField&
Foam::heterogeneousPyrolysisModels::volPyrolysis::surf() const
{
    return cellOnBedBorder_;
}

inline const Foam::heterogeneousPyrolysisModels::SolidState&
Foam::heterogeneousPyrolysisModels::volPyrolysis::solidState() const
{
    return solidState_();
}

inline Foam::label
Foam::heterogeneousPyrolysisModels::volPyrolysis::nNonOrthCorr() const
{
    return nNonOrthCorr_;
}

inline Foam::scalar
Foam::heterogeneousPyrolysisModels::volPyrolysis::maxDiff() const
{
    return maxDiff_;
}

inline Foam::scalar
Foam::heterogeneousPyrolysisModels::volPyrolysis::maxTime() const
{
    return chemistryTimeStep_;
}

inline Foam::Switch
Foam::heterogeneousPyrolysisModels::volPyrolysis::equilibrium() const
{
    return equilibrium_;
}

inline Foam::tmp<Foam::volScalarField>
Foam::heterogeneousPyrolysisModels::volPyrolysis::heatTransfer() const
{
    return heatExchange_->heatTransfer();
}

inline Foam::tmp<Foam::volScalarField>
Foam::heterogeneousPyrolysisModels::volPyrolysis::heatUpGas() const
{
    return heatExchange_->heatUpGas();
}

inline Foam::tmp<Foam::volScalarField>
Foam::heterogeneousPyrolysisModels::volPyrolysis::solidChemistrySh() const
{
    return chemistrySh_;
}
// ************************************************************************* //
