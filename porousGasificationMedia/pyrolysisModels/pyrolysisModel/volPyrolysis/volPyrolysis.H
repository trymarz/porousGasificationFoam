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
    Foam::heterogeneousPyrolysisModels::volPyrolysis

Description

    Reacting and radiating volumetric pyrolysis model
SourceFiles
    volPyrolysis.C

\*---------------------------------------------------------------------------*/

#ifndef volPyrolysis_H
#define volPyrolysis_H

#include "heterogeneousPyrolysisModel.H"
#include "HGSSolidThermo.H"
#include "porousThermoSolidChemistryModel.H"
#include "surfaceFields.H"
#include "solidFluxLimiter.H"
#include "solidDiagnostics.H"
#include "solidState.H"
#include "solidFields.H"
#include "bedCollapse.H"
#include "demCoupling.H"
#include "gasSolidHeatExchange.H"
#include "solidEnergy.H"
#include "porosityRecovery.H"
#include "solidSpeciesTransport.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{


/*---------------------------------------------------------------------------*\
                      Class volPyrolysis Declaration
\*---------------------------------------------------------------------------*/

class volPyrolysis
:
    public heterogeneousPyrolysisModel
{
private:

    // Private member functions

        //- Disallow default bitwise copy construct
        volPyrolysis(const volPyrolysis&);

        //- Disallow default bitwise assignment
        void operator=(const volPyrolysis&);

        //- Read model controls
        void readReactingOneDimControls();

        //- Scale the shared solid flux to what the cells can supply and take
        //  Delegates to solidFluxLimiter_ (two-sided saturation limit) and
        //  stores the result in phiSolid_, the flux every solid field rides.
        void limitSolidFlux();

        //- Ym_i = Yi*rho*(1 - porosity), and the initial solid mass. Patches
        //  where Yi is fixedValue become fixedYm, which keeps Ym current as
        //  rho and porosity change; all other patch types follow Yi.
        void createYm();

        //- solidH = rho*Cp*(1 - porosity)*Ts; fixedValue Ts patches become
        //  fixedSolidH, and cyclic patches are rebuilt on solidH
        void createSolidH();

        //- In solid cells, rebuild Ym from the read-in Yi, then Yi = Ym/sum(Ym)
        void renormaliseInitialComposition();

        //- Build the stages that act on the solid fields, in dependency
        //  order; each logs its own settings when constructed
        void createCollaborators();

        //- Let only cells active and connected at step start react, so
        //  every reaction heat is booked by the solid energy equation
        void markReactingCells();

        //- Re-evaluate the gas-solid heat and specie transfer coefficients
        void updateTransferCoefficients();

        //- Solve the solid chemistry: rates, reaction heat, time step
        void solveChemistry();

        //- A zero, unregistered mass source [kg/m3/s]
        tmp<volScalarField> zeroSrho(const word& name) const;


protected:

    //- Porosity
    volScalarField& porosity_;

    volScalarField porosityArch_;

    //- Reference to specie transport model
    autoPtr<specieTransferModel> specieTransfer_;

    //- Gas-solid specie transfer coefficient, registered as STvol
    volScalarField specieTransferCoeff_;

    //- Thermophysical properties of gaseous species.
    psiReactionThermo& gasThermo_;

    //- Thermophysical properties of solid phase
    HGSSolidThermo& solidThermo_;

    //- Solid chemistry model
    autoPtr<porousThermoSolidChemistryModel<HGSSolidThermo>> solidChemistry_;

    // Reference to solid thermo properties

        //- Density [kg/m3]
        volScalarField& rho_;

        //- Density for the initial moment
        volScalarField rho0_;

        //- List of solid components
        PtrList<volScalarField>& Ys_;

       //- List of solid component mass concentrations [kg/m3]
        //  Ym_i = Yi * rho_s * (1 - porosity) = mass of solid specie i per
        //  unit total cell volume. This is the primary transported variable;
        //  SolidState::deriveYiFromYm() derives the mass fractions Ys_ from it.
        PtrList<volScalarField> Ym_;

        // Non-const access to temperature
        volScalarField& T_;

        // Non-const access to energy of solid phase
        autoPtr<volScalarField> solidH_;

    //- Switch for equilibrium/nonequilibrium model
    Switch equilibrium_;

    // Solution parameters

        //- Number of non-orthogonal correctors
        label nNonOrthCorr_;
        //- Maximum diffussivity
        scalar maxDiff_;

    // Reference to main region fields

        //- Radiation
        volScalarField& radiation_;

    // Fields

        //- Heat release [J/s/m3]
        volScalarField chemistrySh_;

        //- The solid regimes of each cell, and their thresholds
        autoPtr<SolidState> solidState_;

        //- CONV, the interphase exchange and heatUpGas
        autoPtr<GasSolidHeatExchange> heatExchange_;

        //- Ts equation and solidH transport
        autoPtr<SolidEnergy> solidEnergy_;

        //- border cells inside solid body
        volScalarField cellOnBedBorder_;

        //- Solid-phase velocity. Read-only reference to the single "Us"
        //  field registered by the solver (createFields.H). With DEM
        //  coupling it is overwritten each step by lambdaDotModel from the
        //  Laplace-smoothed UsDEM aggregate; without DEM it is read from
        //  0/Us. The solid mass flux and porosity advection consume it.
        const volVectorField& Us_;

        //- The solid volumetric face flux the transport stages ride [m3/s]
        //  Set by limitSolidFlux() from solidFluxLimiter_. Owned here,
        //  not by the limiter: SolidSpeciesTransport and SolidEnergy read it.
        surfaceScalarField phiSolid_;

        //- Reference to the solid flux limiter
        autoPtr<SolidFluxLimiter> solidFluxLimiter_;

        //- Reference to the fail-fast guards on the solid state
        autoPtr<SolidDiagnostics> solidDiagnostics_;

        //- Bed collapse: moves the solid above each emptied cell down
        autoPtr<BedCollapse> bedCollapse_;

        //- Porosity from the solid mass, emptied cells and the masks after
        autoPtr<PorosityRecovery> porosityRecovery_;

    // DEM (YADE) coupling — lambdaDot

        //- Computes lambdaDot in SolidSpeciesTransport; a no-op without DEM
        autoPtr<DemCoupling> demCoupling_;

        //- The solid species mass equations, Ym_ transported
        autoPtr<SolidSpeciesTransport> speciesTransport_;

    // Chemistry

        //- Time step suggested by the last chemistry solve [s]
        scalar chemistryTimeStep_;


    // Protected member functions

        //- Read control parameters
        bool read();

        //- Read control parameters from dict
        bool read(const dictionary& dict);

        //- Report the solid mass budget and the derived mass fractions
        void reportSolidMassBudget();

public:

    //- Runtime type information
    TypeName("volPyrolysis");


    // Constructors

        //- Construct from type name, mesh, main region gas list and radiation
        volPyrolysis
        (
            const word& modelType,
            const fvMesh& mesh,
            HGSSolidThermo& solidThermo,
            psiReactionThermo& gasThermo,
            volScalarField& porosity,
            volScalarField& radiation
        );

    //- Destructor
    virtual ~volPyrolysis();

    // Member Functions

        // Access

            //- Fields

                //- Return density [kg/m3]
                inline virtual volScalarField& rhoConst() const;

                //- Return const temperature [K]
                inline virtual const volScalarField& T() const;

                //- Return specific heat capacity [J/kg/K]
                inline virtual const tmp<volScalarField> Cp() const;

                //- Return the region absorptivity [1/m]
                inline virtual const volScalarField& kappa() const;

                //- Return the region thermal conductivity [W/m/K]
                inline virtual const volScalarField& K() const;

                //- Return list of cells in surface layer inside solid
                inline virtual const volScalarField& surf() const;

                //- The solid regimes of every cell, and their thresholds
                inline virtual const SolidState& solidState() const;

        // Parametrs from main region

            // Solution parameters

                //- Return the number of non-orthogonal correctors
                inline label nNonOrthCorr() const;

                //- Return max diffusivity allowed in the solid
                inline virtual scalar maxDiff() const;

        // Helper functions

            //- Mean diffusion number of the solid region
            virtual scalar solidRegionDiffNo() const;

            //- Time step suggested by the last chemistry solve [s]
            inline virtual scalar maxTime() const;

            //- Whether gas and solid share one temperature
            inline virtual Switch equilibrium() const;

        // Source fields (read/write access)

       // Evolution

            //- Pre-evolve region
            virtual void preEvolveRegion();

            //- Evolve the pyrolysis equations
            virtual void evolveRegion();

        //Comunication with main region

            //- Return total mass source - Eulerian phase only
            virtual Foam::tmp<Foam::volScalarField> Srho() const;

            //- Return mass source for specie i - Eulerian phase only
            virtual Foam::tmp<Foam::volScalarField> Srho
            (
                const label i
            ) const;

            //- Return enthalpy source - Eulerian phase only
            inline virtual tmp<volScalarField> heatTransfer() const;

            //- Return enthalpy needed to heat up gas
            inline virtual tmp<volScalarField> heatUpGas() const;

            //- Heat released by the solid reactions [J/s/m3]
            inline virtual tmp<volScalarField> solidChemistrySh() const;

        // I-O

            //- Provide some feedback
            virtual void info() const;

};

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#include "volPyrolysisI.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
