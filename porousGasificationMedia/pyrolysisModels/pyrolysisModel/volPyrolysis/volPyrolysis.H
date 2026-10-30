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
#include "solidStateChecker.H"
#include "solidState.H"
#include "bedCollapse.H"
#include "lambdaDotCoupling.H"
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

        //- Flag solid cells with an empty neighbour cell
        void markInitialBedBorderCells();

        //- Let only cells active and connected at step start react, so
        //  every reaction heat is booked by the solid energy equation
        void markReactingCells();

        //- Re-evaluate the gas-solid heat and specie transfer coefficients
        void updateTransferCoefficients();

        //- Solve the solid chemistry: rates, reaction heat, time step
        void solveChemistry();


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

    //- List of gas components
    PtrList<volScalarField>& Ygas_;

    //- Thermophysical properties of solid phase
    HGSSolidThermo& solidThermo_;

    //- Solid chemistry model
    autoPtr<porousThermoSolidChemistryModel<HGSSolidThermo>> solidChemistry_;

    // Reference to solid thermo properties

        //- Absorption coefficient [1/m]
        const volScalarField& kappa_;

        //- Thermal conductivity [W/m/K]
        const volScalarField& K_;

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

        //- InitialPorosity
        volScalarField porosity0_;

        volScalarField gasVolumeFractionAtStepStart_;

        //- Radiation
        volScalarField& radiation_;

    // Fields

        //- Heat release [J/s/m3]
        volScalarField chemistrySh_;

        //- The solid regimes of each cell, and their thresholds
        autoPtr<SolidState> solidState_;

        volScalarField cellVolume_;

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
        autoPtr<SolidStateChecker> solidStateChecker_;

        //- Bed collapse: moves the solid above each emptied cell down
        autoPtr<BedCollapse> bedCollapse_;

        //- Porosity from the solid mass, emptied cells and the masks after
        autoPtr<PorosityRecovery> porosityRecovery_;

    // DEM (YADE) coupling — lambdaDot

        //- Computes lambdaDot in SolidSpeciesTransport; a no-op without DEM
        autoPtr<LambdaDotCoupling> lambdaDot_;

        //- The solid species mass equations, Ym_ transported
        autoPtr<SolidSpeciesTransport> speciesTransport_;

    // Checks

        //- Cumulative lost mass of the condensed phase [kg]
        dimensionedScalar lostSolidMass_;

        //- Cumulative mass generation of the gas phase [kg]
        dimensionedScalar addedGasMass_;

        //- Total mass gas flux at the pyrolysing walls [kg/s]
        dimensionedScalar totalGasMassFlux_;

        //- Total heat release rate [J/s]
        dimensionedScalar totalHeatRR_;

        //- Time step suggested by the last chemistry solve [s]
        scalar chemistryTimeStep_;

        //- Total Ym mass integral at construction, for the conservation
        //  diagnostic [kg]. On restart this holds the restart inventory,
        //  so conservation is checked from the restart onward.
        scalar initialTotalYmMass_;


    // Protected member functions

        //- Read control parameters
        bool read();

        //- Read control parameters from dict
        bool read(const dictionary& dict);

        //- Report the solid mass budget and the derived mass fractions
        void reportSolidMassBudget();

        //- Add this step's gas produced and solid lost to the run totals
        void accumulateMassBudget();

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
                virtual volScalarField& rhoConst() const;

                //- Return density [kg/m3]
                virtual volScalarField& rho();

                //- Return const temperature [K]
                virtual const volScalarField& T() const;

                //- Return specific heat capacity [J/kg/K]
                virtual const tmp<volScalarField> Cp() const;

                //- Return the region absorptivity [1/m]
                virtual const volScalarField& kappa() const;

                //- Return the region thermal conductivity [W/m/k]
                virtual const volScalarField& K() const;

                //- Return list of cells in surface layer inside solid
                virtual const volScalarField& surf() const;

                //- The solid regimes of every cell, and their thresholds
                virtual const SolidState& solidState() const;

        // Parametrs from main region

            // Solution parameters

                //- Return the number of non-orthogonal correctors
                inline label nNonOrthCorr() const;

                //- Return max diffusivity allowed in the solid
                virtual scalar maxDiff() const;

        // Helper functions

            //- Mean diffusion number of the solid region
            virtual scalar solidRegionDiffNo() const;

            //- Returm time modification factor
            virtual scalar maxTime() const;

            //- Returm time modification factor
            virtual Switch equilibrium() const;

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
            virtual Foam::tmp<Foam::volScalarField> heatTransfer() const;

            //- Return enthalpy needed to heat up gas
            virtual Foam::tmp<Foam::volScalarField> heatUpGas() const;

            //- Return enthalpy needed to heat up gas  GK  or to pyrolyse the solid into gas ???
            virtual Foam::tmp<Foam::volScalarField> solidChemistrySh() const;

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
