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

    Gas crossing the interface carries the sensible enthalpy of the phase
    it leaves (#212, D4), per species i with the mass source Srho_i that
    feeds rhoEqn and YEqn:

        interfaceEnthalpy = sum_{Srho_i>0} Srho_i*hs_i(Ts)
                          + sum_{Srho_i<0} Srho_i*hs_i(Tg)       (gas EEqn)
        agentHeatUp       = sum_{Srho_i<0} |Srho_i|*(hs_i(Ts) - hs_i(Tg))
                                                                 (solid TEqn, -)

    Products are born at Ts; a consumed agent leaves the gas at Tg and the
    solid heats it to Ts, so the reaction heat hr holds with all
    participants at Ts. The solid needs no product term: the converted
    solid already leaves at Ts (SolidEnergy::recoverTemperatureAfterReaction).
    hs_i is the gas thermo's own, so the gas source is h even when the gas
    solves for e. Neither carries a gate of its own: Srho_i is nonzero only
    in reacting cells, which are the connectedAtStepStart cells.

    The exchange is gated by connectedAtStepStart where it is produced, so
    the solid and gas equations consume one field and the pair conserves
    energy. All three are zero in equilibrium mode.

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
#include "solidFields.H"

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

        //- The owner's switch, bound so a runtime read() takes effect
        const Switch& equilibrium_;

        //- Sensible enthalpy the gas gains with the interface mass [W/m3]
        volScalarField interfaceEnthalpy_;

        //- Heat the solid gives a consumed agent, from Tg to Ts [W/m3]
        volScalarField agentHeatUp_;

        //- Interphase exchange as the solid TEqn consumed it, stored so the
        //  gas EEqn adds back the same joules rather than a fresh evaluation
        //  against a temperature the solid solve has since moved.
        volScalarField heatTransferSh_;

        //- Reference to heat transport model
        autoPtr<porousHeatTransferModel> HTmodel_;

        //- Volumetric heat transfer coefficient CONV [W/m3/K]
        volScalarField heatTransferCoeff_;

        //- Switch for temporal subintegration of heatTransfer model
        Switch subintegrateSwitch_;


    // Private member functions

        //- Compute the interphase exchange from the current state
        tmp<volScalarField> heatTransferCalc() const;

        //- CONV from the heat transfer model, zero where there is no solid
        tmp<volScalarField> heatTransferCoeffCalc() const;

        //- Disallow default bitwise copy construct
        GasSolidHeatExchange(const GasSolidHeatExchange&);

        //- Disallow default bitwise assignment
        void operator=(const GasSolidHeatExchange&);


public:

    // Constructors

        //- Construct from the owner's coefficients and solid fields; selects
        //  the heat transfer model, which also reads porosityArch
        GasSolidHeatExchange
        (
            const dictionary& coeffs,
            const SolidFields& solid,
            const volScalarField& porosityArch,
            const psiReactionThermo& gasThermo,
            const Switch& equilibrium
        );


    //- Destructor
    ~GasSolidHeatExchange();


    // Member Functions

        //- Re-evaluate the coefficient (CONV) from the heat transfer model
        void updateHeatTransferCoeff();

        //- Evaluate and store the exchange for this step
        const volScalarField& updateHeatTransfer();

        //- Evaluate and store interfaceEnthalpy and agentHeatUp from the
        //  per-species gas mass sources, indexed like the gas species
        void updateInterfaceEnthalpy(const PtrList<volScalarField>& SrhoI);

        //- Interphase exchange stored by updateHeatTransfer() [W/m3]
        const volScalarField& heatTransfer() const
        {
            return heatTransferSh_;
        }

        //- Gas-side interface enthalpy [W/m3]
        const volScalarField& interfaceEnthalpy() const
        {
            return interfaceEnthalpy_;
        }

        //- Solid-side agent heat-up [W/m3]
        const volScalarField& agentHeatUp() const
        {
            return agentHeatUp_;
        }
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
