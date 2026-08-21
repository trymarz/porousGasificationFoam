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
        //  Ys_i = Ym_i / sum_j(Ym_j) in every cell that holds solid mass.
        //  Keyed off the mass rather than off whereIs_, which is one stage
        //  stale: rho_ is only a valid mixture density where the mass
        //  fractions sum to one, and recoverPorosity() divides by it.
        //  A cell that has just lost its last solid keeps a defined
        //  composition (first specie unity, rest zero).
        void deriveYiFromYm();

        //- Solid volumetric face flux [m3/s] carried by Us
        //  Zero unless advectSolidFields_ is set. The same flux transports
        //  every solid field there is - the specie masses Ym_ and the solid
        //  enthalpy solidH_ - so they cannot drift apart. The porosity is
        //  not among them: it is recovered from Ym_ (recoverPorosity()).
        tmp<surfaceScalarField> solidVolFlux() const;

        //- Index of the first cell whose value is not finite or lies
        //  outside [lower, upper]; -1 when the field is valid everywhere.
        //  Cells where the optional mask is not positive are skipped: the
        //  solid temperature has no meaning in a cell that holds no solid,
        //  so an empty cell must not be reported as a defect.
        //  Local to this processor: an invalid state on any one rank
        //  aborts the whole job, so no collective is needed.
        label firstInvalidCell
        (
            const volScalarField& fld,
            const scalar lower,
            const scalar upper,
            const scalarField* mask = nullptr
        ) const;

        //- Abort on an impossible solid state, reporting where it is and
        //  whatever neighbouring quantities the calling stage knows are
        //  relevant. The solid transport equations are explicit and
        //  unbounded, so a value outside its physical range is a defect
        //  to be diagnosed, not a number to be clipped and carried on.
        void reportInvalidSolidState
        (
            const word& stage,
            const volScalarField& fld,
            const label cellI,
            const string& context
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

    //- Switch for emptying a flipped cell rather than leaving its solid
    //  A cell above critPorosity_ with Us pointing into the solid is a flip
    //  candidate: the trailing edge of a bed being dragged away, holding the
    //  last of its solid. With the porosity derived from the mass there are
    //  only two consistent answers for such a cell, and this selects between
    //  them.
    //
    //  When unset the candidate is left alone. It keeps the solid it holds
    //  and drains through the ordinary transport, so porosity and mass agree
    //  and nothing is destroyed.
    //
    //  When set the cell is emptied for real - its solid specie masses and
    //  its solid enthalpy are removed - and the porosity recovery then
    //  derives por = 1 from a cell that is genuinely empty. The mass removed
    //  is destroyed rather than moved: finding a receiving cell is what the
    //  bedCollapse route does, and there is none on this path. It is charged
    //  to cumulativeFlipMass_ and reported with the mass budget.
    //
    //  Writing por = 1 while leaving the mass in place - the third option,
    //  and what this did before - is not available. It leaves the cell
    //  claiming to be void while it holds solid, which is the contradiction
    //  the porosity recovery exists to remove, and the solid flux limiter
    //  reads that link to decide how much room a receiving cell has.
    Switch emptyFlippedCells_;

    //- Switch for Eulerian advection of the solid fields by Us
    //  When false the solid volumetric flux is held at zero, so the solid
    //  specie masses and the solid enthalpy are only updated by chemistry
    //  - and so, through the mass, is the recovered porosity.
    Switch advectSolidFields_;

    //- Switch for the fail-fast guards on the solid state
    //  When set, an impossible porosity, Ym, solid enthalpy or solid
    //  temperature aborts the run at the equation that produced it,
    //  rather than propagating into a downstream floating-point
    //  exception whose location says nothing about the cause.
    Switch failOnInvalidSolidState_;

    //- Relative slack admitted by the fail-fast guards [-]
    //  Sized to absorb round-off on the explicit updates only; an
    //  excursion larger than this is a genuine overshoot.
    scalar solidStateTolerance_;

    //- Bound on a physically credible solid temperature [K]
    //  Ts is a ratio, solidH_/(totalYm*Cp), and says nothing credible
    //  about a cell whose solid inventory is vanishing: the linear
    //  solver's error over a collapsing heat capacity is a temperature of
    //  any size at all. This is the upper end of the guard window, and
    //  solidStateTolerance_ times it is the allowance below zero at the
    //  lower end.
    scalar maxSolidTemperature_;

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
        //  -sum_i RRs_i/rho_i, i.e. exactly the time derivative of the
        //  solid volume fraction the chemistry removes. It is reported but
        //  never applied: the porosity is recovered from Ym_, which the
        //  same rates have already changed in solveSpeciesMass().
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
        //  diagnostic [kg]. Accumulated over the species in the
        //  constructor, so on restart it holds the restart inventory and
        //  conservation is checked from the restart onward.
        scalar initialTotalYmMass_;

        //- Cumulative solid mass advected out through boundaries [kg]
        //  Credited in the conservation diagnostic so the budget stays
        //  checkable in domains with an open outlet.
        scalar cumulativeYmOutflow_;

        //- Cumulative mass created by the max(0) clip on Ym [kg]
        //  The clip fabricates mass whenever a numerical undershoot
        //  drives Ym negative. Accumulated in solveSpeciesMass() and
        //  reported with the mass budget so the budget cannot look
        //  closed while the clip is quietly paying for it.
        scalar cumulativeYmClip_;

        //- Cumulative solid mass destroyed by emptying flipped cells [kg]
        //  Nonzero only under emptyFlippedCells_. Reported with the mass
        //  budget so a conserved-looking total cannot hide the solid this
        //  removes.
        scalar cumulativeFlipMass_;


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
            //  1 - porosity = sum_i Ym_i/rho_i, exactly. Also refreshes the
            //  whereIs_ masks and runs the bed-motion model. Must be the
            //  last stage of evolveRegion(): it divides by rho_, which
            //  postSolveEnergy() refreshes.
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
