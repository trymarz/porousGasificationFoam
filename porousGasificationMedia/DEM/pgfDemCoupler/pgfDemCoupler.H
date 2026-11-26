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
    Foam::pgfDemCoupler

Description
    PGF's half of the PGF<->Yade coupling, protocol v2 (#328), active when
    constant/yadeProperties sets `active true;`. A thin adapter over
    PgfDemBridge::FoamSide (submodules/PgfDemBridge, built into this library
    by PgfDemBridge.C): the core owns the handshake, message layout,
    neighbour graph, exchange and teardown; this class only converts.

    Launch: one mpirun starts both programs; the solver runs with -parallel
    -mpi-split-by-appnum -mpi-threads (one Foam rank: a one-piece processor0
    decomposition). Yade's half is the DemSide engine in yade-pgf.

    Per exchange (once per PGF step): each body Yade sends here is
    {pos, vel, lambda}; the rank whose mesh holds its centre answers with
    lambdaDot [m/s] (rate modes) or the apparentVolume factor r, the others
    with NaN. Bodies found here are kept for lambdaDotModel. Then Foam's
    deltaT, and whether this was the run's last step.

SourceFiles
    pgfDemCoupler.C
    PgfDemBridge.C

\*---------------------------------------------------------------------------*/

#ifndef pgfDemCoupler_H
#define pgfDemCoupler_H

#include "fvMesh.H"
#include "volFields.H"
#include "demParticle.H"
#include "PgfDemBridge.hpp"

#include <memory>
#include <vector>

namespace Foam
{

class pgfDemCoupler
{
    const fvMesh& mesh_;
    const volScalarField& lambdaDot_;

    std::unique_ptr<PgfDemBridge::FoamSide> side_;

    demParticleList particles_;
    std::vector<double> values_;

public:

    //- Collective with the DEM ranks: joins the bridge and runs the handshake.
    pgfDemCoupler
    (
        const fvMesh& mesh,
        const volScalarField& lambdaDot,
        PgfDemBridge::LambdaMode lambdaMode
    );

    //- Aborts the run if close() was not called (the DEM side would hang).
    ~pgfDemCoupler();

    //- One exchange: locate the bodies, answer each, send deltaT and lastStep.
    //  Returns false if the DEM side stopped; particles() is then empty.
    bool exchange(scalar deltaT, bool lastStep);

    //- Collective: ends the coupling (stops the DEM side if the run ended early).
    void close();

    //- Bodies located on this rank at the last exchange.
    const demParticleList& particles() const { return particles_; }
};

} // namespace Foam

#endif
