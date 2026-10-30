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

    The transported variable is the solid enthalpy solidH = rhoCp*Ts
    [J/m3], with rhoCp the floored heat capacity of the solid the cell
    holds, sum_i Ym_i*Cp_i. Ts is always recovered from it.

    solve(), before the species step:
      - Ts = solidH/rhoCp, with the trace rule;
      - the Ts equation: conduction (cut at faces on the edge of the
        solid), reaction heat, gas-solid exchange, heatUpGas and
        radiation, the last two gated by connectedAtStepStart;
      - solidH = rhoCp*Ts, then advected with the solid mass flux, so
        enthalpy and Ym share face values.

    recoverTemperatureAfterReaction(), after the species step: Ts is
    recovered on the heat capacity of the pre-reaction solid, so mass
    leaving as gas takes its enthalpy with it, then the dispersed and
    trace rules; solidH is restated on the mass that remains.

    In equilibrium mode only the radiation source is refreshed.

    Owned directly by volPyrolysis (like BedCollapse) - no runtime
    selection.

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
#include "solidStateChecker.H"
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

        //- Solid mass fractions, from solidThermo
        PtrList<volScalarField>& Ys_;

        //- Solid mass concentrations [kg/m3], owned by volPyrolysis
        PtrList<volScalarField>& Ym_;

        //- Solid enthalpy, owned by volPyrolysis and created after this
        autoPtr<volScalarField>& solidH_;

        const volScalarField& porosity_;

        const volVectorField& Us_;

        //- The limited solid volumetric face flux [m3/s]
        const surfaceScalarField& phiSolid_;

        //- Radiative source from the radiation model, ungated
        const volScalarField& radiation_;

        //- Reaction heat [W/m3]
        const volScalarField& chemistrySh_;

        SolidState& solidState_;

        GasSolidHeatExchange& heatExchange_;

        const SolidStateChecker& solidStateChecker_;

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

        //- sum_i Ym_i before this step's reaction [kg/m^3]
        //  Ym_ must already carry the reaction.
        tmp<volScalarField> preReactionMass
        (
            const volScalarField& totalYm
        ) const;

        //- Floored sum_i Ym_i*Cp_i of that pre-reaction solid [J/m^3/K]
        tmp<volScalarField> preReactionHeatCapacity
        (
            const volScalarField& preReactionYm
        );

        //- Ts = solidH/rhoCp, then the dispersed rule (if asked) and
        //  the trace rule
        void recoverTsFromH
        (
            const volScalarField& rhoCp,
            const volScalarField& totalYm,
            const bool dispersedRule
        );

        //- solidH = rhoCp*Ts
        void storeH(const volScalarField& rhoCp);

        //- Solid conduction, cut at faces on the edge of the solid
        tmp<fvScalarMatrix> conductionWithinSolid
        (
            const volTensorField& composedK
        );

        //- Ts equation: conduction, reaction heat, gas exchange, radiation
        void solveTsEquation
        (
            const volTensorField& composedK,
            const volScalarField& rhoCp,
            const volScalarField& solidBeyondTrace
        );

        //- Advect solidH with the solid mass flux
        void advectSolidH
        (
            const volScalarField& rhoCp,
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

        //- Construct from the owner's thermo, fields and collaborators
        SolidEnergy
        (
            const fvMesh& mesh,
            HGSSolidThermo& solidThermo,
            const psiReactionThermo& gasThermo,
            const porousThermoSolidChemistryModel<HGSSolidThermo>& chemistry,
            PtrList<volScalarField>& Ym,
            autoPtr<volScalarField>& solidH,
            const volScalarField& porosity,
            const volVectorField& Us,
            const surfaceScalarField& phiSolid,
            const volScalarField& radiation,
            const volScalarField& chemistrySh,
            SolidState& solidState,
            GasSolidHeatExchange& heatExchange,
            const SolidStateChecker& solidStateChecker,
            const Switch& equilibrium,
            const Switch& active
        );


    //- Destructor
    ~SolidEnergy();


    // Member Functions

        //- Ts from solidH, the Ts equation, then solidH advected
        void solve();

        //- Ts recovered after the reaction changed the mass, and
        //  solidH restated on the mass that remains
        void recoverTemperatureAfterReaction();
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
