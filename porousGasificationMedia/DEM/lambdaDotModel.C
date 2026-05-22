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

Description
    Implementation of \c lambdaDotModel. See the class Description in
    the .H for the field inventory and the per-time-step sequence.
    Function-level briefs on \c update() and \c writeParticlesData()
    are inline below.

\*---------------------------------------------------------------------------*/

#include "lambdaDotModel.H"
#include "IOdictionary.H"
#include "fvmLaplacian.H"

namespace Foam
{

lambdaDotModel::lambdaDotModel
(
    const fvMesh& mesh,
    volScalarField& lambdaDot,
    volScalarField& nParticles,
    volVectorField& UsDEM,
    volVectorField& Us,
    volScalarField& porosityF,
    FoamYade& yade
)
:
    mesh_(mesh),
    lambdaDot_(lambdaDot),
    nParticles_(nParticles),
    UsDEM_(UsDEM),
    Us_(Us),
    porosityF_(porosityF),
    yade_(yade),
    // Read the user-supplied lambdaDot(y) profile from
    // constant/lambdaDict. Currently the function is evaluated at the
    // cell-centre y-coordinate, so it is effectively a 1D vertical
    // profile.
    lambdaFunc_
    (
        Function1<scalar>::New
        (
            "lambdaDot",
            IOdictionary
            (
                IOobject
                (
                    "lambdaDict",
                    mesh.time().constant(),
                    mesh,
                    IOobject::MUST_READ,
                    IOobject::NO_WRITE
                )
            ),
            &mesh
        )
    )
{}


//- One-step update of the DEM<->fluid bridge.
//
//  Sequence:
//    1. Refresh lambdaDot_ per cell from lambdaFunc_(c.y()).
//    2. Sum particle velocities into UsDEM_ per cell, count
//       particles into nParticles_. Average UsDEM_ where
//       nParticles_ > 0, zero it elsewhere.
//    3. Build Us_ from UsDEM_ as a starting point, then mask gas-only
//       cells (porosityF >= 0.999) to zero.
//    4. Smooth Us_ by solving a Laplace equation
//          fvm::laplacian(1, Us_) == 0
//       which fills in solid cells that had no DEM particle and
//       smooths the jumps between particle-bearing and empty solid
//       cells.
//    5. Re-mask gas-only cells to zero (the Laplace solve can leak
//       finite values into them).
//    6. Push lambdaDot back into each particle (only where the cell
//       contains at least one).
void lambdaDotModel::update()
{
    // Step 1: cell-wise lambdaDot from the user function.
    forAll(lambdaDot_, cellI)
    {
        const point& c = mesh_.C()[cellI];
        lambdaDot_[cellI] = lambdaFunc_->value(c.y());
    }

    lambdaDot_.correctBoundaryConditions();

    // Step 2: count particles per cell and sum their velocities.
    nParticles_ = 0.0;
    UsDEM_ = dimensionedVector("zero", UsDEM_.dimensions(), vector::zero);

    for (const auto& procPtr : yade_.inCommProcs)
    {
        if (!procPtr) continue;

        for (const auto& partPtr : procPtr->foundParticles)
        {
            if (!partPtr) continue;

            label cellI = mesh_.findCell(partPtr->pos);
            if (cellI >= 0)
            {
                nParticles_[cellI] += 1.0;
                UsDEM_[cellI] += partPtr->linearVelocity;
            }
        }
    }

    // Average velocity in occupied cells; zero in empty cells.
    forAll(UsDEM_, cellI)
    {
        if (nParticles_[cellI] > 0.5)
        {
            UsDEM_[cellI] /= nParticles_[cellI];
        }
        else
        {
            UsDEM_[cellI] = vector::zero;
        }
    }

    UsDEM_.correctBoundaryConditions();

    // Step 3: seed Us_ from UsDEM_, then mask gas-only cells.
    Us_ = UsDEM_;

    forAll(Us_, cellI)
    {
        if (porosityF_[cellI] >= 0.999)
        {
            Us_[cellI] = vector::zero;
        }
    }

    // Step 4: Laplace-smooth Us_ so empty solid cells (cells inside
    // the bed without a particle) get a continuous velocity. Diffusion
    // coefficient = 1 is dimensionless because we only care about the
    // steady-state distribution.
    fvVectorMatrix UsEqn
    (
        fvm::laplacian
            (
                dimensionedScalar("one", dimless, 1.0),
                Us_
            )
    );

    UsEqn.solve();

    // Step 5: re-mask gas-only cells in case the Laplace solve leaked
    // non-zero values into them through their solid neighbours.
    forAll(Us_, cellI)
    {
        if (porosityF_[cellI] >= 0.999)
        {
            Us_[cellI] = vector::zero;
        }
    }

    // Step 6: push lambdaDot back to each particle (occupied cells only).
    for (const auto& procPtr : yade_.inCommProcs)
    {
        if (!procPtr) continue;

        for (const auto& partPtr : procPtr->foundParticles)
        {
            if (!partPtr) continue;

            label cellI = mesh_.findCell(partPtr->pos);

            if (cellI < 0) continue;
            if (nParticles_[cellI] < 0.5) continue;

            partPtr->lambdaDot = lambdaDot_[cellI];
        }
    }
}


//- Append per-particle (id, cellID, lambdaDot) rows to
//  \c <time>/ParticlesData.txt for the current rank. Only writes at
//  output-time steps. The file is opened in append mode so multiple
//  ranks can each contribute their own block.
void lambdaDotModel::writeParticlesData() const
{
    if (!mesh_.time().outputTime()) return;

    const int myRank = Pstream::myProcNo();

    fileName outDir = mesh_.time().timePath();
    mkDir(outDir);

    fileName outPath = outDir / "ParticlesData.txt";

    std::ofstream ofs(outPath.c_str(), std::ios::app);
    ofs.setf(std::ios::scientific);
    ofs.precision(8);

    ofs << "# rank " << myRank
        << " time " << mesh_.time().timeName()
        << " (particleID cellID lambdaDot)\n";

    for (const auto& procPtr : yade_.inCommProcs)
    {
        if (!procPtr) continue;

        for (const auto& partPtr : procPtr->foundParticles)
        {
            if (!partPtr) continue;

            label cellI = mesh_.findCell(partPtr->pos);
            if (cellI < 0) continue;
            if (nParticles_[cellI] < 0.5) continue;

            ofs << partPtr->indx << " "
                << cellI << " "
                << lambdaDot_[cellI] << "\n";
        }
    }

    ofs << "\n";
}

} // namespace Foam
