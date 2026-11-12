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
    Foam::heterogeneousPyrolysisModels::DemCoupling

Description
    The pyrolysis side of the DEM (YADE) coupling: computes lambdaDot, the
    rate of change of the DEM particle length scale [m/s] (or under
    lambdaMode apparentVolume the factor r [-] YADE applies once), which
    lambdaDotModel then pushes to the YADE particles.

    Each step lambdaDot is reset (beginStep), each solid specie adds its
    chemistry-driven term in the species step (addChemistryTerm), the
    temperature-driven term follows the energy step (addTemperatureTerm),
    and the boundary values follow once all have contributed (finishStep).
    Which model computes the terms is selected by lambdaMode in
    constant/lambdaDict; see doc/case-setup.md.

    Coupling is active only when constant/yadeProperties has active=true
    and the solver registered the lambdaDot/lambda fields. Otherwise
    every call is a no-op, and in a build without WITH_YADE it always is.

    Owned directly by volPyrolysis (like BedCollapse) - no runtime
    selection; the lambdaDot model itself is selected at runtime.

SourceFiles
    demCoupling.C

\*---------------------------------------------------------------------------*/

#ifndef demCoupling_H
#define demCoupling_H

#include "fvMesh.H"
#include "volFields.H"
#include "basicSolidMixture.H"

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
                     Class DemCoupling Declaration
\*---------------------------------------------------------------------------*/

class DemCoupling
{
    // Private data

        //- DEM coupling is active for this case: yadeProperties active=true
        //  and lambdaDot/lambda registered. Resolved once at construction.
        //  A WITH_YADE build can still run a non-DEM case.
        bool demActive_;

        //- Rate of change of the DEM particle length scale [m/s], from
        //  createDEMFields.H. Null when !demActive_.
        volScalarField* lambdaDotPtr_;

#ifdef WITH_YADE
        //- lambdaDot strategy, selected by lambdaMode in constant/lambdaDict.
        //  Created only when demActive_.
        autoPtr<LambdaDotCalculationModel> lamDotCalc_;
#endif


    // Private member functions

        //- Disallow default bitwise copy construct
        DemCoupling(const DemCoupling&);

        //- Disallow default bitwise assignment
        void operator=(const DemCoupling&);


public:

    // Constructors

        //- Construct from the mesh and the solid species, whose rhoApparent
        //  the model may need; resolves whether coupling is active and logs it
        DemCoupling(const fvMesh& mesh, const basicSolidMixture& composition);


    //- Destructor
    ~DemCoupling();


    // Member Functions

        //- Whether DEM coupling is active for this case
        bool active() const
        {
            return demActive_;
        }

        //- Reset lambdaDot to zero for this step
        void beginStep();

        //- Add the temperature-driven term; reads this step's solved Ts
        void addTemperatureTerm();

        //- Add the chemistry-driven term of one solid specie; Ym_i is its
        //  mass concentration before this step's solve
        void addChemistryTerm
        (
            const volScalarField& sRhoSi,
            const volScalarField& Ym_i,
            const word& specie
        );

        //- Update lambdaDot boundary values once every term is in
        void finishStep();
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
