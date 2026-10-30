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
    const fvMesh& mesh,
    const volScalarField& porosity,
    const volScalarField& porosityArch,
    const psiReactionThermo& gasThermo,
    const HGSSolidThermo& solidThermo,
    const volScalarField& T,
    const volScalarField& rho,
    const SolidState& solidState,
    const Switch& equilibrium,
    const Switch& active
)
:
    mesh_(mesh),
    gasThermo_(gasThermo),
    solidThermo_(solidThermo),
    T_(T),
    rho_(rho),
    solidState_(solidState),
    equilibrium_(equilibrium),
    active_(active),
    heatUpGas_
    (
        IOobject
        (
            "heatUpGas",
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
    HTmodel_(porousHeatTransferModel::New(porosity,porosityArch)),
    CONV_
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
{}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

GasSolidHeatExchange::~GasSolidHeatExchange()
{}


// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //

Foam::tmp<Foam::volScalarField> GasSolidHeatExchange::heatTransferCalc() const
{
// eqZx2uHGn005
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
                if (solidState_.solidPresent()[cellI] == 1.0 && CONV_[cellI] > 0.0)
                {
                    deltaTemp[cellI] =
                        (Tgas[cellI]
                         * (rhoCpG[cellI]
                           +rhoCpS[cellI]
                           *exp(-(CONV_[cellI] * deltaTime*(rhoCpS[cellI]+rhoCpG[cellI]))
                                /(rhoCpS[cellI]*rhoCpG[cellI])))
                           +rhoCpS[cellI]*T_[cellI]
                           *(1 - exp( -(CONV_[cellI]*deltaTime*(rhoCpS[cellI]+rhoCpG[cellI]))/(rhoCpS[cellI]*rhoCpG[cellI])))
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

            volScalarField HT(CONV_*(T_-Tgas));
            Info << "The heat transfer subintegration info:" << nl
                 << " no subintegration min, max:" << gMin(HT) << " " << gMax(HT) << nl
                 << " subintegration    min, max:" << gMin(Sh_()) << " " << gMax(Sh_()) << endl;
        }
        else
        {
            // This works only for small CONV otherwise oscillations appear.
            volScalarField Tgas = gasThermo_.T();
            volScalarField HT(CONV_*(T_-Tgas));
            Sh_ = HT * connectedAtStepStart;
        }
    }
    return Sh_;
}

Foam::tmp<Foam::volScalarField> GasSolidHeatExchange::CONV() const
{
    Foam::tmp<Foam::volScalarField> CONVloc_ = Foam::tmp<Foam::volScalarField>
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
        CONVloc_ = HTmodel_->CONV()*solidState_.solidPresent();
    }
    return CONVloc_;
}

Foam::tmp<Foam::volScalarField> GasSolidHeatExchange::heatUpGasCalc
(
    const volScalarField& Srho
) const
{

    Foam::tmp<Foam::volScalarField> hSh_ = Foam::tmp<Foam::volScalarField>
    (
        new volScalarField
        (
            IOobject
            (
                "hSh_",
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

    if (active_)
    {
        volScalarField gasCp = gasThermo_.Cp();

        if (!equilibrium_) // eqZx2uHGn018
        {
            // Gated at the producer, so both consumers keep seeing one field.
            // The pair already conserved; what the gate adds is a bound, the
            // same one its three siblings in TEqn have.
            volScalarField tempSh = hSh_();
            tempSh = gasThermo_.Cp() * (T_ - gasThermo_.T()) * Srho;
            hSh_ = tempSh * solidState_.connectedAtStepStart() * solidState_.solidFraction();
        }
    }

    return hSh_;
}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

void GasSolidHeatExchange::updateCONV()
{
    CONV_ = CONV();
}


const volScalarField& GasSolidHeatExchange::updateHeatTransfer()
{
    heatTransferSh_ = heatTransferCalc()();
    return heatTransferSh_;
}


void GasSolidHeatExchange::updateHeatUpGas(const volScalarField& Srho)
{
    heatUpGas_ = heatUpGasCalc(Srho)();
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// ************************************************************************* //
