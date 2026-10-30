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
    Foam::heterogeneousPyrolysisModels::SolidSpeciesTransport

Description
    Solid species mass transport.

    The transported variables are the solid mass concentrations Ym_i
    [kg/m3], mass of specie i per unit total cell volume. For each
    specie solve() integrates

        d(Ym_i)/dt = RRs_i - div(phiSolid, Ym_i)

    on the already-limited solid flux, aborts on an invalid result,
    clips negative Ym_i to zero (the mass that creates is booked in
    clippedMass()), then derives the mass fractions Ys from Ym.

    The DEM lambdaDot is assembled alongside: its temperature term at
    the start, a chemistry term per specie, completed at the end.

    Owned directly by volPyrolysis (like BedCollapse) - no runtime
    selection.

SourceFiles
    solidSpeciesTransport.C

\*---------------------------------------------------------------------------*/

#ifndef solidSpeciesTransport_H
#define solidSpeciesTransport_H

#include "fvMesh.H"
#include "volFields.H"
#include "surfaceFields.H"
#include "PtrList.H"
#include "Switch.H"
#include "HGSSolidThermo.H"
#include "porousThermoSolidChemistryModel.H"
#include "solidState.H"
#include "solidStateChecker.H"
#include "lambdaDotCoupling.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

/*---------------------------------------------------------------------------*\
                    Class SolidSpeciesTransport Declaration
\*---------------------------------------------------------------------------*/

class SolidSpeciesTransport
{
    // Private data

        const fvMesh& mesh_;

        //- Solid thermo, for the specie names
        const HGSSolidThermo& solidThermo_;

        const porousThermoSolidChemistryModel<HGSSolidThermo>& chemistry_;

        //- Solid specie mass concentrations [kg/m3], owned by volPyrolysis
        PtrList<volScalarField>& Ym_;

        //- Solid mass fractions, from solidThermo
        PtrList<volScalarField>& Ys_;

        const volScalarField& porosity_;

        const volVectorField& Us_;

        //- The limited solid volumetric face flux [m3/s]
        const surfaceScalarField& phiSolid_;

        //- DEM lambdaDot, assembled during the species step
        LambdaDotCoupling& lambdaDot_;

        const SolidStateChecker& solidStateChecker_;

        //- The owner's switch, bound so a runtime read() takes effect
        const Switch& active_;

        //- Cumulative mass created by the max(0) clip on Ym [kg]
        //  Reported with the mass budget, so the budget cannot look closed
        //  while the clip is quietly paying for it.
        scalar cumulativeYmClip_;


    // Private member functions

        //- Abort if Ym_i is invalid after its equation
        void checkSpecieMass
        (
            const label i,
            const volScalarField& divYmFlux,
            const volScalarField& sRhoSi
        ) const;

        //- Clip negative Ym_i to zero; the mass added is booked
        void clipNegativeSpecieMass(volScalarField& Ym_i);

        //- Solve the mass equation of solid specie i
        void solveSpecieMass(const label i);

        //- Disallow default bitwise copy construct
        SolidSpeciesTransport(const SolidSpeciesTransport&);

        //- Disallow default bitwise assignment
        void operator=(const SolidSpeciesTransport&);


public:

    //- Runtime type information, for the debug switch
    ClassName("SolidSpeciesTransport");


    // Constructors

        //- Construct from the owner's thermo, fields and collaborators
        SolidSpeciesTransport
        (
            const fvMesh& mesh,
            const HGSSolidThermo& solidThermo,
            const porousThermoSolidChemistryModel<HGSSolidThermo>& chemistry,
            PtrList<volScalarField>& Ym,
            PtrList<volScalarField>& Ys,
            const volScalarField& porosity,
            const volVectorField& Us,
            const surfaceScalarField& phiSolid,
            LambdaDotCoupling& lambdaDot,
            const SolidStateChecker& solidStateChecker,
            const Switch& active
        );


    //- Destructor
    ~SolidSpeciesTransport();


    // Member Functions

        //- Cumulative mass created by the max(0) clip on Ym [kg]
        scalar clippedMass() const
        {
            return cumulativeYmClip_;
        }

        //- Solve solid species mass conservation, then Ys from Ym
        void solve();
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
