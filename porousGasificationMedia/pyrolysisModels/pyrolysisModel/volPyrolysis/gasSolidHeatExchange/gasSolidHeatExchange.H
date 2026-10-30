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

Class
    Foam::heterogeneousPyrolysisModels::GasSolidHeatExchange

Description
    Heat exchanged between the gas and the solid in the non-equilibrium
    (two-temperature) model, as source terms shared by the solid TEqn and
    the gas EEqn [W/m3].

    CONV is the volumetric heat transfer coefficient [W/m3/K] from the
    runtime-selected porousHeatTransferModel (heatTransferProperties),
    zeroed where there is no solid. The exchange is CONV*(Ts - Tg); with
    subintegrateHeatTransfer it is the analytic two-temperature relaxation
    over the time step instead, stable for large CONV.

    heatUpGas is Cp_g*(Ts - Tg)*Srho: the heat that brings the gas the
    solid releases from the gas to the solid temperature.

    Both are gated by connectedAtStepStart where they are produced, so the
    solid and gas equations consume one field and the pair conserves
    energy. Both are zero in equilibrium mode.

    Owned directly by volPyrolysis (like BedCollapse) - no runtime
    selection; the heat transfer model itself is selected at runtime.

SourceFiles
    gasSolidHeatExchange.C

\*---------------------------------------------------------------------------*/

#ifndef gasSolidHeatExchange_H
#define gasSolidHeatExchange_H

#include "fvMesh.H"
#include "volFields.H"
#include "Switch.H"
#include "psiReactionThermo.H"
#include "HGSSolidThermo.H"
#include "porousHeatTransferModel.H"
#include "solidState.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

/*---------------------------------------------------------------------------*\
                   Class GasSolidHeatExchange Declaration
\*---------------------------------------------------------------------------*/

class GasSolidHeatExchange
{
    // Private data

        const fvMesh& mesh_;

        const psiReactionThermo& gasThermo_;

        const HGSSolidThermo& solidThermo_;

        //- Solid temperature and density
        const volScalarField& T_;
        const volScalarField& rho_;

        const SolidState& solidState_;

        //- The owner's switches, bound so a runtime read() takes effect
        const Switch& equilibrium_;
        const Switch& active_;

        volScalarField heatUpGas_;

        //- Interphase exchange as the solid TEqn consumed it, stored so the
        //  gas EEqn adds back the same joules rather than a fresh evaluation
        //  against a temperature the solid solve has since moved.
        volScalarField heatTransferSh_;

        //- Reference to heat transport model
        autoPtr<porousHeatTransferModel> HTmodel_;

        volScalarField CONV_;

        //- Switch for temporal subintegration of heatTransfer model
        Switch subintegrateSwitch_;


    // Private member functions

        //- Compute the interphase exchange from the current state
        tmp<volScalarField> heatTransferCalc() const;

        //- CONV from the heat transfer model, zero where there is no solid
        tmp<volScalarField> CONV() const;

        //- Enthalpy needed to heat up the released gas
        tmp<volScalarField> heatUpGasCalc(const volScalarField& Srho) const;

        //- Disallow default bitwise copy construct
        GasSolidHeatExchange(const GasSolidHeatExchange&);

        //- Disallow default bitwise assignment
        void operator=(const GasSolidHeatExchange&);


public:

    // Constructors

        //- Construct from the owner's coefficients and fields; selects the
        //  heat transfer model
        GasSolidHeatExchange
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
        );


    //- Destructor
    ~GasSolidHeatExchange();


    // Member Functions

        //- Whether the exchange is subintegrated over the time step
        const Switch& subintegrate() const
        {
            return subintegrateSwitch_;
        }

        //- Re-evaluate CONV from the heat transfer model
        void updateCONV();

        //- Evaluate and store the exchange for this step
        const volScalarField& updateHeatTransfer();

        //- Evaluate and store heatUpGas from the gas mass source
        void updateHeatUpGas(const volScalarField& Srho);

        //- Interphase exchange stored by updateHeatTransfer() [W/m3]
        const volScalarField& heatTransfer() const
        {
            return heatTransferSh_;
        }

        //- Enthalpy needed to heat up gas [W/m3]
        const volScalarField& heatUpGas() const
        {
            return heatUpGas_;
        }
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
