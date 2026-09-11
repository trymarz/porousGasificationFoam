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

#ifdef WITH_YADE
    // liblambdaDotModel links YADE, so it is referenced only here.
    #include "LambdaDotCalculationModel.H"
#endif
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

        //- Derive mass fractions Ys_ from mass concentrations Ym_
        void deriveYiFromYm();

        //- Scale the shared solid flux to what the cells can supply and take
        //  Delegates to solidFluxLimiter_ (two-sided saturation limit) and
        //  stores the result in phiSolid_, the flux every solid field rides.
        void limitSolidVolFlux();

        //- Abort if any cell's porosity left [minPorosity, 1]
        //  Below the floor a cell has no gas volume, and every gas equation
        //  divides by it. Called each time recoverPorosity() writes porosity_.
        void guardPorosity
        (
            const word& stage,
            const volScalarField& por,
            const surfaceScalarField& phiUs
        ) const;


protected:

    //- Porosity
    volScalarField& porosity_;

    volScalarField porosityArch_;

    //- Reference to specie transport model
    autoPtr<specieTransferModel> STmodel_;

    volScalarField ST_;

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
        //  the mass fractions Ys_ are derived from it (see deriveYiFromYm()).
        PtrList<volScalarField> Ym_;

        // Non-const access to temperature
        volScalarField& T_;

        // Non-const access to energy of solid phase
        autoPtr<volScalarField> solidH_;

    //- Name of the radiative flux in the primary region
    word primaryRadFluxName_;

    ///-Switch for equilibrium/nonequilibrium model
    Switch equilibrium_;

    //- Switch for temporal subintegration of heatTransfer model
    Switch subintegrateSwitch_;

    //- Switch for bed motion model
    Switch bedCollapseSwitch_;

    //- Switch for bed replenishment
    Switch replenishSwitch_;

    //- Switch for bed motion that moves the solid rather than the porosity
    //  Set, bed motion moves Ym_/solidH_ down the route by the same
    //  cell-volume ratio it applies to porosity; unset, only porosity moves.
    Switch collapseMovesSolidMass_;

    //- Switch for emptying a flipped cell rather than leaving its solid
    //  A flip candidate: above critPorosity_ with Us pointing inward. Set,
    //  it is emptied for real; mass destroyed, charged to cumulativeFlipMass_.
    Switch emptyFlippedCells_;

    //- Switch for taking a nearly-empty cell's solid temperature from the gas
    //  Above critPorosity_, Ts = solidH/(totalYm*Cp) is a frozen numerator
    //  over a vanishing denominator. Set, energy lost this way is uncharged.
    Switch gasTemperatureBelowCriticalPorosity_;

    //- Criterion for bed motion porosity model
    scalar critPorosity_;

    //- Absolute tolerance for detecting incoming solid flux [kg/m3/s]
    scalar poroProtectSolidInflowFluxTolerance_;

    //- Total replenished mass
    scalar totRepMass_;

    // Solution parameters

        //- Number of non-orthogonal correctors
        label nNonOrthCorr_;
        //- Maximum diffussivity
        scalar maxDiff_;

    // Reference to main region fields

        //- InitialPorosity
        volScalarField porosity0_;

        volScalarField voidFraction_;

        //- Dadiation
        volScalarField& radiation_;

    // Fields

        //- Sensible enthalpy gas flux [J/m2/s]
        volScalarField phiHsGas_;

        //- Heat release [J/s/m3]
        volScalarField chemistrySh_;

        //- Porosity growth from chemistry [1/s], diagnostic only
        //  -sum_i RRs_i/rho_i, the rate chemistry removes solid volume at.
        //  Reported, never applied: porosity is recovered from Ym_ instead.
        volScalarField porositySource_;

        //- Masks for equations
        volScalarField whereIs_;

        volScalarField whereIsNot_;

        volScalarField whereWas_;

        volScalarField cellVolume_;

        volScalarField heatUpGas_;

        //- Reference to heat transport model
        autoPtr<heatTransferModel> HTmodel_;

        volScalarField CONV_;

        volScalarField radiationSh_;

        volTensorField anisotropyK_;

        //- border cells inside solid body
        volScalarField surfF_;

        //- Solid-phase velocity. Read-only reference to the single "Us"
        //  field registered by the solver (createFields.H). With DEM
        //  coupling it is overwritten each step by lambdaDotModel from the
        //  Laplace-smoothed UsDEM aggregate; without DEM it is read from
        //  0/Us. The solid mass flux and porosity advection consume it.
        const volVectorField& Us_;

        //- The solid volumetric face flux the transport stages ride [m3/s]
        //  Set by limitSolidVolFlux() from solidFluxLimiter_. Owned here,
        //  not by the limiter: solveSpeciesMass()/preSolveEnergy() read it too.
        surfaceScalarField phiSolid_;

        //- Reference to the solid flux limiter
        autoPtr<SolidFluxLimiter> solidFluxLimiter_;

        //- Reference to the fail-fast guards on the solid state
        autoPtr<SolidStateChecker> solidStateChecker_;

    // DEM (YADE) coupling — lambdaDot

        //- DEM coupling is active for this case: yadeProperties active=true
        //  and lambdaDot/lambda registered. Resolved once at construction,
        //  and always false without WITH_YADE. A WITH_YADE build can still
        //  run a non-DEM case.
        bool demActive_;

        //- Rate of change of the DEM particle length scale [m/s], from
        //  createDEMFields.H. Computed in solveSpeciesMass() and pushed to
        //  the YADE particles by lambdaDotModel. Null when !demActive_.
        volScalarField* lambdaDotPtr_;

#ifdef WITH_YADE
        //- lambdaDot strategy, selected by lambdaMode in constant/lambdaDict.
        //  Created only when demActive_.
        autoPtr<LambdaDotCalculationModel> lamDotCalc_;
#endif

    // Checks

        //- Cumulative lost mass of the condensed phase [kg]
        dimensionedScalar lostSolidMass_;

        //- Cumulative mass generation of the gas phase [kg]
        dimensionedScalar addedGasMass_;

        //- Total mass gas flux at the pyrolysing walls [kg/s]
        dimensionedScalar totalGasMassFlux_;

        //- Total heat release rate [J/s]
        dimensionedScalar totalHeatRR_;

        //- scalar chemistryTime
        scalar timeChem_;

        //- Total Ym mass integral at construction, for the conservation
        //  diagnostic [kg]. On restart this holds the restart inventory,
        //  so conservation is checked from the restart onward.
        scalar initialTotalYmMass_;

        //- Cumulative solid mass advected out through boundaries [kg]
        //  Credited in the conservation diagnostic so the budget stays
        //  checkable in domains with an open outlet.
        scalar cumulativeYmOutflow_;

        //- Cumulative mass created by the max(0) clip on Ym [kg]
        //  Reported with the mass budget, so the budget cannot look closed
        //  while the clip is quietly paying for it.
        scalar cumulativeYmClip_;

        //- Cumulative solid mass destroyed by emptying flipped cells [kg]
        //  Nonzero only under emptyFlippedCells_; reported with the mass
        //  budget so it isn't hidden in a conserved-looking total.
        scalar cumulativeFlipMass_;

        //- Cumulative solid mass shed at the foot of a bed-motion route [kg]
        //  Nonzero only under collapseMovesSolidMass_. A route spanning
        //  processors can also create mass not charged here - see recoverPorosity().
        scalar cumulativeCollapseMass_;


    // Protected member functions

        //- Read control parameters
        bool read();

        //- Read control parameters from dict
        bool read(const dictionary& dict);

        //- Mass check
        void calculateMassTransfer();

        // Equations

            //- Solve solid species mass conservation
            void solveSpeciesMass();

            //- Solve energy
            void preSolveEnergy();

            //- Solve energy
            void postSolveEnergy();

            //- Recover the porosity from the transported solid mass
            //  1 - porosity = sum_i Ym_i/rho_i, exactly. Last stage of
            //  evolveRegion(): divides by the rho_ postSolveEnergy() sets.
            void recoverPorosity();

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
            volScalarField& whereIs,
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
            virtual Foam::tmp<Foam::volScalarField> heatTransfer();

            //- Return CONV
            virtual Foam::tmp<Foam::volScalarField> CONV() const;

            //- Return enthalpy needed to heat up gas
            virtual Foam::tmp<Foam::volScalarField> heatUpGasCalc() const;

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
