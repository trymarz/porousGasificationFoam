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
    Foam::lambdaDotModel

Description
    DEM-side bridge between YADE particles and the gas-phase solver.

    Only compiled when the solver is built with \c WITH_YADE. Driven
    from the main loop's DEM block via \c update() once per step. Owns
    references (not ownership) to four fields that move information
    in both directions:

      - \c lambdaDot_  — per-cell scalar rate-of-change of a per-particle
                          lambda variable. Set from a user-supplied
                          \c Function1<scalar> read from
                          \c constant/lambdaDict at construction;
                          pushed back into the particles after the
                          per-cell aggregation step.
                          TODO: walk through with the user the
                          semantics of "lambda" here — physical
                          interpretation is not obvious from the
                          implementation (currently evaluates the
                          function at c.y(), i.e. as a 1D vertical
                          profile).
      - \c nParticles_ — number of YADE spheres whose centre falls in
                          each cell. Used to average UsDEM and to gate
                          empty cells.
      - \c UsDEM_     — per-cell average particle velocity. The raw
                          DEM aggregate; non-smooth, only populated
                          where particles live.
      - \c Us_        — smoothed solid-velocity field actually seen
                          by the rest of the solver (volPyrolysis,
                          UEqn-side advection). Built by Laplace-
                          smoothing UsDEM_ into adjacent solid cells
                          (cells with porosityF < 0.999), with the
                          gas-only cells (porosityF >= 0.999) clamped
                          to zero before and after the smoothing.

    \c writeParticlesData() dumps a per-rank ParticlesData.txt at every
    write-time with one row per particle (particleID, cellID,
    lambdaDot) for downstream post-processing.

SourceFiles
    lambdaDotModel.H
    lambdaDotModel.C

\*---------------------------------------------------------------------------*/

#ifndef lambdaDotModel_H
#define lambdaDotModel_H

#include "fvMesh.H"
#include "FoamYade.H"
#include "Function1.H"

namespace Foam
{

class lambdaDotModel
{
private:
    const fvMesh& mesh_;

    //- Per-cell rate-of-change of the per-particle lambda variable.
    //  Sampled from lambdaFunc_ at the cell centre's y-coordinate.
    volScalarField& lambdaDot_;

    //- Number of YADE spheres in each cell (computed in update()).
    volScalarField& nParticles_;

    //- Raw per-cell average particle velocity (only set where
    //  nParticles_ > 0, zero elsewhere).
    volVectorField& UsDEM_;

    //- Laplace-smoothed solid velocity actually consumed by the rest
    //  of the solver. See class Description for the smoothing path.
    volVectorField& Us_;

    //- Porosity field used to mask the smoothing to porous cells only.
    volScalarField& porosityF_;

    FoamYade& yade_;

    //- User-supplied lambdaDot(y) profile read from constant/lambdaDict.
    autoPtr<Function1<scalar>> lambdaFunc_;


public:
    lambdaDotModel
    (
        const fvMesh& mesh,
        volScalarField& lambdaDot,
        volScalarField& nParticles,
        volVectorField& UsDEM,
        volVectorField& Us,
        volScalarField& porosityF,
        FoamYade& yade
    );

    //- Once-per-time-step update. See the .C for the full sequence.
    void update();

    //- Append per-particle lambdaDot diagnostic to
    //  \c <time>/ParticlesData.txt (one file per rank).
    void writeParticlesData() const;

};

}

#endif // lambdaDotModel_H
