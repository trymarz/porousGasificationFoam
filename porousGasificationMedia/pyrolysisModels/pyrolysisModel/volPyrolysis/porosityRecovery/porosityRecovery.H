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
    Porosity recovery, the last stage of the solid step.

    Porosity is not transported: 1 - porosity = sum_i Ym_i/rho_i, an
    identity of the solid mixture density, so recover() first restates
    it from the transported mass. It then picks the dispersed or trace
    cells to empty - inside the bed or on a receding edge, unless solid
    is still flowing in - and hands them to the bed collapse or, under
    emptyFlippedCells, removes their solid (mass destroyed, charged to
    flippedMass()). Last it refreshes the solid masks, the bed-border
    flags and the connectedAtStepStart gate the next step reads.

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
#include "solidStateChecker.H"
#include "bedCollapse.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

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

        //- Solid enthalpy, owned by volPyrolysis and created after this
        autoPtr<volScalarField>& solidH_;

        const volVectorField& Us_;

        //- The limited solid volumetric face flux [m3/s]
        const surfaceScalarField& phiSolid_;

        const porousThermoSolidChemistryModel<HGSSolidThermo>& chemistry_;

        //- Border cells inside the solid body, owned by volPyrolysis
        volScalarField& cellOnBedBorder_;

        SolidState& solidState_;

        BedCollapse& bedCollapse_;

        //- Fail-fast guards, owned by volPyrolysis and created after this
        const autoPtr<SolidStateChecker>& solidStateChecker_;

        //- The owner's switches, bound so a runtime read() takes effect
        const Switch& active_;
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

        //- Abort if any cell's solid fraction left [0, maxSolidFraction]
        //  Below the floor a cell has no gas volume, and every gas equation
        //  divides by it. Called each time recover() writes porosity_.
        void guardPorosity
        (
            const word& stage,
            const volScalarField& por,
            const surfaceScalarField& phiUs
        ) const;

        //- porosity = 1 - totalYm/rho on the internal field, then guarded
        void porosityFromSolidMass(const volScalarField& totalYm);

        //- Dispersed or trace cells to empty, less those solid is still
        //  flowing into; nFlips returns their global count
        labelList cellsToEmpty
        (
            const volScalarField& totalYm,
            label& nFlips
        );

        //- Every rank's cells to empty, on every rank
        List<Field<label>> gatherFlipLists
        (
            const labelList& flipCells
        ) const;

        //- Remove the solid of the cells to empty; mass destroyed
        void emptyCells(const labelList& flipCells, const label nFlips);

        //- Flag solid cells that face a partly solid face
        void markBedBorderCells();

        //- Disallow default bitwise copy construct
        PorosityRecovery(const PorosityRecovery&);

        //- Disallow default bitwise assignment
        void operator=(const PorosityRecovery&);


public:

    // Constructors

        //- Construct from the owner's coefficients, fields and collaborators
        PorosityRecovery
        (
            const dictionary& coeffs,
            const fvMesh& mesh,
            volScalarField& porosity,
            volScalarField& T,
            const volScalarField& rho,
            PtrList<volScalarField>& Ym,
            PtrList<volScalarField>& Ys,
            autoPtr<volScalarField>& solidH,
            const volVectorField& Us,
            const surfaceScalarField& phiSolid,
            const porousThermoSolidChemistryModel<HGSSolidThermo>& chemistry,
            volScalarField& cellOnBedBorder,
            SolidState& solidState,
            BedCollapse& bedCollapse,
            const autoPtr<SolidStateChecker>& solidStateChecker,
            const Switch& active,
            const Switch& infoOutput
        );


    //- Destructor
    ~PorosityRecovery();


    // Member Functions

        //- Whether a flipped cell is emptied rather than left
        const Switch& emptyFlippedCells() const
        {
            return emptyFlippedCells_;
        }

        //- Incoming solid flux that protects a cell from emptying [kg/m3/s]
        scalar poroProtectSolidInflowFluxTolerance() const
        {
            return poroProtectSolidInflowFluxTolerance_;
        }

        //- Cumulative solid mass destroyed by emptying flipped cells [kg]
        scalar flippedMass() const
        {
            return cumulativeFlipMass_;
        }

        //- Recover the porosity from the transported solid mass
        //  1 - porosity = sum_i Ym_i/rho_i, exactly. Last stage of
        //  evolveRegion(): divides by the rho_ recoverTemperatureAfterReaction() sets.
        void recover();
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
