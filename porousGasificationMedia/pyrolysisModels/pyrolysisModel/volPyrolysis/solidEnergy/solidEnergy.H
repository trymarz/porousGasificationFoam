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
    Foam::heterogeneousPyrolysisModels::SolidEnergy

Description
    Solid-phase energy of the non-equilibrium (two-temperature) model.

    The conserved variable is the solid enthalpy solidH = rhoCp*Ts
    [J/m3], with rhoCp the floored heat capacity of the solid the cell
    holds, sum_i Ym_i*Cp_i.

    solve(), after the species step, integrates one balance for it,

        d(solidH)/dt + div(sum_i phiYm_i*Cp_i*Ts) - laplacian(K, Ts)
            = sources + sum_i RRs_i*Cp_i*Ts

    with Ts as the unknown, so conduction stays implicit:
      - the old-time term is solidH as the last step left it;
      - the diagonal is the carrier heat capacity, sum_i Cp_i times the
        mass before this step's reaction: the last term, the sensible heat
        of the composition change, taken implicitly, so mass leaving as gas
        takes its enthalpy with it;
      - enthalpy rides each specie's own mass flux, upwind and explicit;
      - conduction is cut at faces on the edge of the solid;
      - sources: reaction heat and radiation gated by connectedAtStepStart,
        gas-solid exchange and the agent heat-up (GasSolidHeatExchange).

    Then the dispersed and trace rules on Ts, once, and solidH = rhoCp*Ts
    on the mass that remains.

    In equilibrium mode only the radiation source is refreshed.

    Owned directly by volPyrolysis (like BedCollapse) - no runtime
    selection.

    Field access (equation):
      reads   Ym, rho, K, phiYm passed in, the radiation, reaction and
              exchange sources, Tg, the SolidState masks and gate; Us and
              porosity in the abort report;
      writes  Ts, the unknown, and solidH from it; solidThermo.correct();
              owns radiationSh.

SourceFiles
    solidEnergy.C

\*---------------------------------------------------------------------------*/

#ifndef solidEnergy_H
#define solidEnergy_H

#include "fvMesh.H"
#include "volFields.H"
#include "surfaceFields.H"
#include "fvMatricesFwd.H"
#include "PtrList.H"
#include "Switch.H"
#include "psiReactionThermo.H"
#include "HGSSolidThermo.H"
#include "porousThermoSolidChemistryModel.H"
#include "solidState.H"
#include "solidFields.H"
#include "solidDiagnostics.H"
#include "gasSolidHeatExchange.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

/*---------------------------------------------------------------------------*\
                        Class SolidEnergy Declaration
\*---------------------------------------------------------------------------*/

class SolidEnergy
{
    // Private data

        const fvMesh& mesh_;

        HGSSolidThermo& solidThermo_;

        const psiReactionThermo& gasThermo_;

        const porousThermoSolidChemistryModel<HGSSolidThermo>& chemistry_;

        //- Solid temperature, density and conductivity, from solidThermo
        volScalarField& T_;
        const volScalarField& rho_;
        const volScalarField& K_;

        //- Solid mass concentrations [kg/m3], owned by volPyrolysis
        const PtrList<volScalarField>& Ym_;

        //- Solid enthalpy, owned by volPyrolysis. Counts from 0 K, the gas he
        //  from 298.15 K; only an energy audit sees the difference. To be
        //  unified in #173 (#212 D6).
        volScalarField& solidH_;

        const volScalarField& porosity_;

        const volVectorField& Us_;

        //- The limited solid volumetric face flux [m3/s]
        const surfaceScalarField& phiSolid_;

        //- Radiative source from the radiation model, ungated
        const volScalarField& radiation_;

        //- Reaction heat [W/m3]
        const volScalarField& chemistrySh_;

        const SolidState& solidState_;

        const GasSolidHeatExchange& heatExchange_;

        const SolidDiagnostics& solidDiagnostics_;

        //- The owner's switches, bound so a runtime read() takes effect
        const Switch& equilibrium_;
        const Switch& active_;

        //- Radiative source the solid takes this step [W/m3]
        volScalarField radiationSh_;

        //- Conductivity anisotropy, identity unless read
        volTensorField anisotropyK_;


    // Private member functions

        //- Floored heat capacity of the solid a cell holds [J/m^3/K]
        tmp<volScalarField> solidHeatCapacity
        (
            const volScalarField& totalYm
        ) const;

        //- Floored sum_i Cp_i*max(Ym_i - dt*RRs_i, 0): the solid that holds
        //  the old solidH plus what transport brought [J/m^3/K]
        //  Ym_ must already carry this step's reaction and transport.
        tmp<volScalarField> carrierHeatCapacity
        (
            const volScalarField& totalYm
        ) const;

        //- The dispersed rule, then the trace rule, on Ts
        void applyRegimeRules(const volScalarField& totalYm);

        //- solidH = rhoCp*Ts
        void storeH(const volScalarField& rhoCp);

        //- Solid conduction, cut at faces on the edge of the solid
        tmp<fvScalarMatrix> conductionWithinSolid
        (
            const volTensorField& composedK
        );

        //- Sources [W/m3]: reaction heat and radiation gated by
        //  connectedAtStepStart, minus gas exchange and agent heat-up
        tmp<volScalarField> solidSources();

        //- div of the enthalpy flux, sum_i phiYm_i*upwind(Cp_i*Ts) [W/m3]
        tmp<volScalarField> advectiveEnthalpyFlux
        (
            const PtrList<surfaceScalarField>& phiYm
        ) const;

        //- The solidH balance, solved for Ts
        void solveEnthalpyBalance
        (
            const volTensorField& composedK,
            const volScalarField& rhoCpCarrier,
            const volScalarField& HOld,
            const PtrList<surfaceScalarField>& phiYm
        );

        //- Abort with the cell's energy terms if badCell is a cell
        void abortOnInvalidTs
        (
            const word& stage,
            const label badCell,
            const volScalarField& HOld,
            const volScalarField& rhoCpCarrier,
            const volScalarField& totalYm
        ) const;

        //- After the balance: check Ts, clip it at zero, correct the
        //  thermo and apply the regime rules, then check Ts again
        void settleTs
        (
            const volScalarField& HOld,
            const volScalarField& rhoCpCarrier,
            const volScalarField& totalYm
        );

        //- Disallow default bitwise copy construct
        SolidEnergy(const SolidEnergy&);

        //- Disallow default bitwise assignment
        void operator=(const SolidEnergy&);


public:

    //- Runtime type information, for the debug switch
    ClassName("SolidEnergy");


    // Constructors

        //- Construct from the solid fields, gas thermo, heat sources and stages
        SolidEnergy
        (
            const SolidFields& solid,
            const psiReactionThermo& gasThermo,
            const volScalarField& radiation,
            const volScalarField& chemistrySh,
            const GasSolidHeatExchange& heatExchange,
            const SolidDiagnostics& solidDiagnostics,
            const Switch& equilibrium
        );


    //- Destructor
    ~SolidEnergy();


    // Member Functions

        //- The solidH balance on the mass the species step left, then the
        //  regime rules on Ts; phiYm is that step's specie mass fluxes
        void solve(const PtrList<surfaceScalarField>& phiYm);
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
