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
    Foam::heterogeneousPyrolysisModels::PorosityRecovery

Description
    Porosity recovery and the emptying of flipped cells, stages of the
    solid step that volPyrolysis::evolveRegion() calls one by one.

    Porosity is not transported: 1 - porosity = sum_i Ym_i/rho_i, an
    identity of the solid mixture density, so updatePorosity() restates
    it from the transported mass. findFlipCells() picks the dispersed or
    trace cells to empty - inside the bed or on a receding edge, unless
    solid is still flowing in - for the bed collapse or, under
    emptyFlippedCells, for emptyCells(), which removes their solid (mass
    destroyed, charged to flippedMass()). markBedBorderCells() flags the
    solid cells on the bed border.

    Each write of porosity is guarded: a solid fraction outside
    [0, maxSolidFraction] aborts the run.

    Reads emptyFlippedCells and poroProtectSolidInflowFluxTolerance from
    the owning model's coefficients, and owns porosityS, the diagnostic
    chemistry source. Owned directly by volPyrolysis (like BedCollapse) -
    no runtime selection.

SourceFiles
    porosityRecovery.C

\*---------------------------------------------------------------------------*/

#ifndef porosityRecovery_H
#define porosityRecovery_H

#include "fvMesh.H"
#include "volFields.H"
#include "surfaceFields.H"
#include "PtrList.H"
#include "Switch.H"
#include "HGSSolidThermo.H"
#include "porousThermoSolidChemistryModel.H"
#include "solidState.H"
#include "solidFields.H"
#include "solidDiagnostics.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

/*---------------------------------------------------------------------------*\
                         Struct FlipCells Declaration
\*---------------------------------------------------------------------------*/

//- The cells to empty this step
struct FlipCells
{
    //- On this rank
    labelList local;

    //- Every rank's list, on every rank
    List<Field<label>> allRanks;

    //- Number over all ranks
    label nGlobal;
};


/*---------------------------------------------------------------------------*\
                      Class PorosityRecovery Declaration
\*---------------------------------------------------------------------------*/

class PorosityRecovery
{
    // Private data

        const fvMesh& mesh_;

        //- Porosity of the owning model
        volScalarField& porosity_;

        //- Solid temperature, zeroed in an emptied cell
        volScalarField& T_;

        //- Solid density [kg/m3]
        const volScalarField& rho_;

        //- Solid specie mass concentrations [kg/m3]
        PtrList<volScalarField>& Ym_;

        //- Solid mass fractions, rebuilt from Ym_ after mass moves
        PtrList<volScalarField>& Ys_;

        //- Solid enthalpy, owned by volPyrolysis
        volScalarField& solidH_;

        const volVectorField& Us_;

        //- The limited solid volumetric face flux [m3/s]
        const surfaceScalarField& phiSolid_;

        const porousThermoSolidChemistryModel<HGSSolidThermo>& chemistry_;

        //- Border cells inside the solid body, owned by volPyrolysis
        volScalarField& cellOnBedBorder_;

        const SolidState& solidState_;

        //- Fail-fast guards, owned by volPyrolysis
        const SolidDiagnostics& solidDiagnostics_;

        //- The owner's switch, bound so a runtime read() takes effect
        const Switch& infoOutput_;

        //- Switch for emptying a flipped cell rather than leaving its solid
        //  A flip candidate: dispersed or trace, inside or on a receding edge. Set,
        //  it is emptied for real; mass destroyed, charged to cumulativeFlipMass_.
        Switch emptyFlippedCells_;

        //- Absolute tolerance for detecting incoming solid flux [kg/m3/s]
        scalar poroProtectSolidInflowFluxTolerance_;

        //- Porosity growth from chemistry [1/s], diagnostic only
        //  -sum_i RRs_i/rho_i, the rate chemistry removes solid volume at.
        //  Reported, never applied: porosity is recovered from Ym_ instead.
        volScalarField porositySource_;

        //- Cumulative solid mass destroyed by emptying flipped cells [kg]
        //  Nonzero only under emptyFlippedCells_; reported with the mass
        //  budget so it isn't hidden in a conserved-looking total.
        scalar cumulativeFlipMass_;


    // Private member functions

        //- Disallow default bitwise copy construct
        PorosityRecovery(const PorosityRecovery&);

        //- Disallow default bitwise assignment
        void operator=(const PorosityRecovery&);


public:

    // Constructors

        //- Construct from the owner's coefficients, solid fields and guards
        PorosityRecovery
        (
            const dictionary& coeffs,
            const SolidFields& solid,
            volScalarField& cellOnBedBorder,
            const SolidDiagnostics& solidDiagnostics,
            const Switch& infoOutput
        );


    //- Destructor
    ~PorosityRecovery();


    // Member Functions

        //- Cumulative solid mass destroyed by emptying flipped cells [kg]
        scalar flippedMass() const
        {
            return cumulativeFlipMass_;
        }

        //- Whether flipped cells are emptied when the bed does not collapse
        bool emptyFlippedCells() const
        {
            return emptyFlippedCells_;
        }

        //- porosity = 1 - sum_i Ym_i/rho_i on the internal field, guarded
        //  Divides by the rho_ SolidEnergy::solve() sets. Also evaluates
        //  porosityS, from the step's Ts.
        void updatePorosity();

        //- Dispersed or trace cells to empty, less those solid is still
        //  flowing into. Reads last step's solidPresent.
        FlipCells findFlipCells() const;

        //- Remove the solid of the cells to empty; mass destroyed
        void emptyCells(const FlipCells& flips);

        //- Abort if any cell's solid fraction left [0, maxSolidFraction]
        //  Below the floor a cell has no gas volume, and every gas equation
        //  divides by it. Called after each write of porosity_.
        void guardPorosity(const word& stage) const;

        //- Flag solid cells that face a partly solid face
        //  Reads solidPresent and its boundary values as they stand.
        void markBedBorderCells();
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
