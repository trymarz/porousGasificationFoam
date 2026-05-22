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
    Volumetric heterogeneous pyrolysis model — the concrete
    implementation behind \c pyrolysisZone in the main solver.

    "Volumetric" here means: the solid phase is treated as a per-cell
    bulk quantity weighted by \c (1 - porosityF), not as a discretised
    second region. The model owns:

      - \c rho_, \c Ys_, \c Ym_:        solid density and per-component
                                        mass fractions / mass
                                        densities (Ym = Ys * rho_),
      - \c T_:                          solid temperature,
      - \c porosity_, \c porosityArch_: current porosity field and an
                                        archival copy at t=0,
      - \c whereIs_, \c whereIsNot_:    binary masks selecting cells
                                        that currently contain solid
                                        (porosity_ < 1) and cells that
                                        do not,
      - \c surfF_:                      mask for the surface layer
                                        cells inside the solid body
                                        (consumed by radiation),
      - \c Us_:                         smoothed solid velocity field
                                        (zero unless DEM or bed motion
                                        is enabled),
      - \c chemistrySh_, \c heatUpGas_, \c radiationSh_:
                                        the three explicit source
                                        terms feeding the solid
                                        energy equation,
      - \c CONV_:                       volumetric heat-transfer
                                        coefficient h*SAV [W/m^3/K].

    Two coupling sub-models are constructed and held by autoPtr:

      - \c HTmodel_ (heatTransferModel) — provides \c CONV() and
        feeds the convective interphase heat transfer
        \c CONV*(T_s - T_g);
      - \c STmodel_ (specieTransferModel) — provides the
        diffusion-limited correction \c ST published into the
        chemistry model.

    The chemistry is handled by \c solidChemistry_, a
    \c porousThermoSolidChemistryModel that owns the heterogeneous
    reaction list and the per-cell ODE solver. Its outputs
    (\c RRs / \c RRg / \c RRpor / \c Sh) drive solid species, gas
    species, porosity and the heat-of-reaction term respectively.

    Per-time-step sequence in \c evolveRegion():
      1. cache porosity into \c voidFraction_, refresh \c CONV_ and
         \c ST_;
      2. integrate the per-cell chemistry ODE
         (\c solidChemistry_->solve), publish chemistrySh_ and
         heatUpGas_;
      3. \c solveSpeciesMass — transport equations for Ys_;
      4. \c evolvePorosity — porosity evolution plus optional bed
         collapse (gravity-driven motion through cells with
         porosity above \c criticalPorosity);
      5. \c solidThermo_.correct — refresh K, Cp, kappa from the new
         composition;
      6. \c solveEnergy — solid temperature equation with optional
         non-orthogonal correctors;
      7. \c calculateMassTransfer + \c info — diagnostics.

    Switches read from \c constant/pyrolysisProperties:
      - \c subintegrateHeatTransfer: analytic integration of the
        interphase heat transfer over the time step (more accurate
        for large CONV*dt);
      - \c bedCollapse: gravity-driven motion of partially-converted
        cells through their upward neighbour;
      - \c criticalPorosity: porosity threshold above which a cell
        is considered "empty" and triggers bed motion;
      - \c replenish: continuously refill the top cell of each motion
        path (otherwise it goes to porosity 1).

    The model implements both runtime-selection-table entries — with
    and without radiation — but the main solver always uses the
    radiation-aware constructor.

SourceFiles
    volPyrolysis.C
    volPyrolysisI.H

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

       //- List of solid component masses
        PtrList<volScalarField> Ym_;

        //- List of solid component masses [kg]
        PtrList<volScalarField> Msolid_;

        //- solid total mass [kg]
        volScalarField Msolidtotal_;

        // Non-const access to temperature
        volScalarField& T_;

    //- Name of the radiative flux in the primary region
    word primaryRadFluxName_;

    ///-Switch for equilibrium/nonequilibrium model
    Switch equilibrium_;

    //- Switch for temporal subintegration of heatTransfer model
    Switch subintegrateSwitch_;

    //- Switch for bed motion model
    Switch bedMotionSwitch_;

    //- Switch for bed replenishment
    Switch replenishSwitch_;

    //- Criterion for bed motion porosity model
    scalar critPorosity_;

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

        //- Porosity growth [1/s]
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

        //- solid phase velocity
        volVectorField Us_;

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


    // Protected member functions

        //- Read control parameters
        bool read();

        //- Read control parameters from dict
        bool read(const dictionary& dict);

        //- Mass check
        void calculateMassTransfer();

        // Equations

            //- Solve energy
            void solveEnergy();

            //- Solve solid species mass conservation
            void solveSpeciesMass();

public:

    //- Runtime type information
    TypeName("volPyrolysis");


    // Constructors

        //- Construct from type name, mesh and main region gas list
        volPyrolysis
        (
            const word& modelType,
            const fvMesh& mesh,
            HGSSolidThermo& solidThermo,
            psiReactionThermo& gasThermo,
            volScalarField& whereIs
        );

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

            //- Evolve porosity field
            virtual void evolvePorosity();

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
