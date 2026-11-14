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
    Foam::pgfYadeCoupler

Description
    PGF-owned OpenFOAM half of the minimal PGF<->Yade coupling (#271),
    selected by `coupler pgf;` in constant/yadeProperties. Its Yade half is
    the PgfCoupling engine in the Foam-Yade fork; neither side includes the
    other's headers, so the wire format lives in both and the startup
    handshake checks it.

    Wire protocol, version 1 (tags 27xx; the legacy FoamYade uses 10xx):
      handshake  each OF rank -> every Yade rank: HELLO int[6] {magic,
                 version, strideUp 8, strideDown 2, lambdaMode, nFoamRanks}
                 and BBOX double[6] (its mesh points' min, max). Yade rank 0
                 -> each OF rank: ACK int[6] {magic, version, status,
                 firstWorker, nWorkers, 0}. A mismatch stops both sides
                 with a message naming it; a missing ACK times out.
      exchange   (once per PGF step) each Yade worker -> each OF rank:
                 COUNT n, DATA double[8n] {bodyId, pos, vel, lambda_}.
                 Each OF rank -> each worker: REPLY_COUNT m, REPLY
                 double[2m] {bodyId, value} for the m bodies whose centre
                 it found, so only the finding rank sets a body (#229).
                 OF rank 0 -> Yade rank 0: FOAM_DT, which Yade checks
                 against its dt in rate mode (#230).
    value is lambdaDot [m/s] (rate modes) or the apparentVolume factor r.
    No drag, torque, radius or angular velocity is exchanged.

SourceFiles
    pgfYadeCoupler.C

\*---------------------------------------------------------------------------*/

#ifndef pgfYadeCoupler_H
#define pgfYadeCoupler_H

#include "fvMesh.H"
#include "volFields.H"
#include "demParticle.H"
#include <mpi.h>

namespace Foam
{

class pgfYadeCoupler
{
public:

    static constexpr int protocolVersion = 1;
    static constexpr int magic = 0x50474659;   // "PGFY"
    static constexpr int strideUp = 8;         // Yade -> PGF doubles per body
    static constexpr int strideDown = 2;       // PGF -> Yade doubles per body

private:

    enum tags
    {
        TAG_HELLO = 2701, TAG_BBOX = 2702, TAG_ACK = 2703,
        TAG_COUNT = 2711, TAG_DATA = 2712,
        TAG_REPLY_COUNT = 2713, TAG_REPLY = 2714,
        TAG_FOAM_DT = 2721
    };

    const fvMesh& mesh_;
    const volScalarField& lambdaDot_;
    const int lambdaMode_;

    MPI_Comm interComm_;
    int nYade_;
    int firstWorker_;
    int nWorkers_;

    demParticleList particles_;

    [[noreturn]] void fail(const std::string& msg) const;
    void handshake(scalar timeout);

public:

    //- Joins the Yade job that spawned this solver and runs the handshake.
    pgfYadeCoupler
    (
        const fvMesh& mesh,
        const volScalarField& lambdaDot,
        int lambdaMode,
        scalar handshakeTimeout
    );

    //- One exchange: receive and locate the bodies, return this rank's
    //  value for each one found, then send deltaT.
    void exchange(scalar deltaT);

    //- Bodies located on this rank at the last exchange.
    const demParticleList& particles() const { return particles_; }
};

} // namespace Foam

#endif
