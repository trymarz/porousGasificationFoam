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

#include "gasSolidHeatExchange.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

GasSolidHeatExchange::GasSolidHeatExchange
(
    const dictionary& coeffs,
    const SolidFields& solid,
    const volScalarField& porosityArch,
    const psiReactionThermo& gasThermo,
    const Switch& equilibrium
)
:
    mesh_(solid.mesh),
    gasThermo_(gasThermo),
    solidThermo_(solid.thermo),
    T_(solid.T),
    rho_(solid.rho),
    solidState_(solid.state),
    equilibrium_(equilibrium),
    interfaceEnthalpy_
    (
        IOobject
        (
            "interfaceEnthalpy",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimEnergy/dimTime/dimVolume, 0.0)
    ),
    agentHeatUp_
    (
        IOobject
        (
            "agentHeatUp",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimEnergy/dimTime/dimVolume, 0.0)
    ),
    heatTransferSh_
    (
        IOobject
        (
            "pyrolysisSh",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimEnergy/dimTime/dimVolume, 0.0)
    ),
    HTmodel_(porousHeatTransferModel::New(solid.porosity,porosityArch)),
    heatTransferCoeff_
    (
        IOobject
        (
            "CONVvol",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero",dimEnergy/dimTime/dimTemperature/dimVolume , 0.0)
    ),
    subintegrateSwitch_
    (
        coeffs.lookupOrDefault("subintegrateHeatTransfer",false)
    )
{
    Info << "subintegrateHeatTransfer " << subintegrateSwitch_ << endl;
}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

GasSolidHeatExchange::~GasSolidHeatExchange()
{}


// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //

Foam::tmp<Foam::volScalarField> GasSolidHeatExchange::heatTransferCalc() const
{
    Foam::tmp<Foam::volScalarField> Sh_ = Foam::tmp<Foam::volScalarField>
    (
        new volScalarField
        (
            IOobject
            (
                "pyrolysisSh",
                mesh_.time().timeName(),
                mesh_,
                IOobject::NO_READ,
                IOobject::AUTO_WRITE,
                false
            ),
            mesh_,
            dimensionedScalar("zero", dimEnergy/dimVolume/dimTime, 0.0)
        )
    );

    if (!equilibrium_)
    {
        // Gate the emitted exchange at the producer so the solid TEqn and the
        // gas EEqn consume the same field: an ungated gas side would exchange
        // heat with a solid whose own equation refuses it, creating energy.
        const volScalarField& connectedAtStepStart = solidState_.connectedAtStepStart();

        if (subintegrateSwitch_)
        {
            // Keep the subintegrated gas/solid exchange defined in a fully
            // solid cell. The physical gas capacity vanishes there, while
            // the finite floor prevents an ill-conditioned analytic update.
            volScalarField rhoCpG
            (
                gasThermo_.rho()
              * gasThermo_.Cp()
              * solidState_.gasFractionWithFloor()
            );
            volScalarField Tgas = gasThermo_.T();
            volScalarField rhoCpS
            (
                solidState_.withSolidHeatCapacityFloor
                (
                    rho_ * solidThermo_.Cp() * solidState_.solidFraction()
                )
            );
            volScalarField deltaTemp(T_ * 0);
            scalar deltaTime = mesh_.time().deltaTValue();

            forAll(deltaTemp,cellI)
            {
                if (solidState_.solidPresent()[cellI] == 1.0 && heatTransferCoeff_[cellI] > 0.0)
                {
                    deltaTemp[cellI] =
                        (Tgas[cellI]
                         * (rhoCpG[cellI]
                           +rhoCpS[cellI]
                           *exp(-(heatTransferCoeff_[cellI] * deltaTime*(rhoCpS[cellI]+rhoCpG[cellI]))
                                /(rhoCpS[cellI]*rhoCpG[cellI])))
                           +rhoCpS[cellI]*T_[cellI]
                           *(1 - exp( -(heatTransferCoeff_[cellI]*deltaTime*(rhoCpS[cellI]+rhoCpG[cellI]))/(rhoCpS[cellI]*rhoCpG[cellI])))
                        )
                        / (rhoCpS[cellI]+rhoCpG[cellI]) - Tgas[cellI];
                }
                else
                {
                    deltaTemp[cellI] = 0;
                }
            }

            forAll(Sh_(),cellI)
            {
                Sh_.ref()[cellI] = deltaTemp[cellI] * rhoCpG[cellI] * connectedAtStepStart[cellI] / deltaTime;
            }

            volScalarField HT(heatTransferCoeff_*(T_-Tgas));
            Info << "The heat transfer subintegration info:" << nl
                 << " no subintegration min, max:" << gMin(HT) << " " << gMax(HT) << nl
                 << " subintegration    min, max:" << gMin(Sh_()) << " " << gMax(Sh_()) << endl;
        }
        else
        {
            // This works only for small CONV otherwise oscillations appear.
            volScalarField Tgas = gasThermo_.T();
            volScalarField HT(heatTransferCoeff_*(T_-Tgas));
            Sh_ = HT * connectedAtStepStart;
        }
    }
    return Sh_;
}

Foam::tmp<Foam::volScalarField> GasSolidHeatExchange::heatTransferCoeffCalc() const
{
    Foam::tmp<Foam::volScalarField> tCoeff = Foam::tmp<Foam::volScalarField>
    (
        new volScalarField
        (
            IOobject
            (
                "CONV",
                mesh_.time().timeName(),
                mesh_,
                IOobject::NO_READ,
                IOobject::AUTO_WRITE,
                false
            ),
            mesh_,
            dimensionedScalar("zero", dimEnergy/dimTemperature/dimTime/dimVolume, 0.0)
        )
    );

    if (!equilibrium_)
    {
        tCoeff = HTmodel_->CONV()*solidState_.solidPresent();
    }
    return tCoeff;
}

// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

void GasSolidHeatExchange::updateHeatTransferCoeff()
{
    heatTransferCoeff_ = heatTransferCoeffCalc();
}


const volScalarField& GasSolidHeatExchange::updateHeatTransfer()
{
    heatTransferSh_ = heatTransferCalc()();
    return heatTransferSh_;
}


void GasSolidHeatExchange::updateInterfaceEnthalpy
(
    const PtrList<volScalarField>& SrhoI
)
{
    scalarField& gasSide = interfaceEnthalpy_.primitiveFieldRef();
    scalarField& solidSide = agentHeatUp_.primitiveFieldRef();
    gasSide = 0;
    solidSide = 0;

    if (equilibrium_)
    {
        return;
    }

    const basicSpecieMixture& gas = gasThermo_.composition();
    const scalarField& p = gasThermo_.p();
    const scalarField& Tg = gasThermo_.T();

    forAll(SrhoI, i)
    {
        const scalarField& S = SrhoI[i];

        forAll(S, celli)
        {
            // Products leave the solid at Ts; agents leave the gas at Tg.
            if (S[celli] > 0)
            {
                gasSide[celli] += S[celli]*gas.Hs(i, p[celli], T_[celli]);
            }
            else if (S[celli] < 0)
            {
                const scalar hsTg = gas.Hs(i, p[celli], Tg[celli]);
                gasSide[celli] += S[celli]*hsTg;
                solidSide[celli] -=
                    S[celli]*(gas.Hs(i, p[celli], T_[celli]) - hsTg);
            }
        }
    }

    interfaceEnthalpy_.correctBoundaryConditions();
    agentHeatUp_.correctBoundaryConditions();
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// ************************************************************************* //
