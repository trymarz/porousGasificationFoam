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

\*---------------------------------------------------------------------------*/

#include "pgfYadeCoupler.H"
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

namespace Foam
{

void pgfYadeCoupler::fail(const std::string& msg) const
{
    std::cerr
        << "pgfYadeCoupler (rank " << Pstream::myProcNo() << "): " << msg
        << std::endl;
    MPI_Abort(interComm_ != MPI_COMM_NULL ? interComm_ : MPI_COMM_WORLD, 1);
    std::abort();
}


pgfYadeCoupler::pgfYadeCoupler
(
    const fvMesh& mesh,
    const volScalarField& lambdaDot,
    int lambdaMode,
    scalar handshakeTimeout
)
:
    mesh_(mesh),
    lambdaDot_(lambdaDot),
    lambdaMode_(lambdaMode),
    interComm_(MPI_COMM_NULL),
    nYade_(0),
    firstWorker_(0),
    nWorkers_(0)
{
    MPI_Comm parent;
    MPI_Comm_get_parent(&parent);
    if (parent == MPI_COMM_NULL)
    {
        fail
        (
            "not started by Yade: 'coupler pgf' needs the solver spawned by"
            " a DEM script running the PgfCoupling engine"
        );
    }
    MPI_Intercomm_merge(parent, 1, &interComm_);
    MPI_Comm_remote_size(parent, &nYade_);
    handshake(handshakeTimeout);
}


void pgfYadeCoupler::handshake(scalar timeout)
{
    // Mesh-point bounds of this rank only; mesh.bounds() is reduced globally.
    point lo(GREAT, GREAT, GREAT);
    point hi(-GREAT, -GREAT, -GREAT);
    for (const point& p : mesh_.points())
    {
        lo = min(lo, p);
        hi = max(hi, p);
    }

    std::array<int, 6> hello
    {
        magic, protocolVersion, strideUp, strideDown, lambdaMode_,
        Pstream::nProcs()
    };
    std::array<double, 6> box{lo.x(), lo.y(), lo.z(), hi.x(), hi.y(), hi.z()};

    std::vector<MPI_Request> reqs(2*nYade_);
    for (int y = 0; y < nYade_; ++y)
    {
        MPI_Isend(hello.data(), 6, MPI_INT, y, TAG_HELLO, interComm_, &reqs[2*y]);
        MPI_Isend(box.data(), 6, MPI_DOUBLE, y, TAG_BBOX, interComm_, &reqs[2*y+1]);
    }

    // A legacy FoamCoupling engine never answers, so wait with a deadline.
    const auto start = std::chrono::steady_clock::now();
    for (int flag = 0; !flag; )
    {
        MPI_Iprobe(0, TAG_ACK, interComm_, &flag, MPI_STATUS_IGNORE);
        const double waited = std::chrono::duration<double>
            (std::chrono::steady_clock::now() - start).count();
        if (!flag && waited > timeout)
        {
            std::ostringstream m;
            m << "no handshake reply from Yade within " << timeout << " s."
              << " 'coupler pgf' needs PgfCoupling() in the DEM script;"
              << " the legacy FoamCoupling engine needs 'coupler foamYade'.";
            fail(m.str());
        }
        if (!flag) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    std::array<int, 6> ack{};
    MPI_Recv(ack.data(), 6, MPI_INT, 0, TAG_ACK, interComm_, MPI_STATUS_IGNORE);
    MPI_Waitall(int(reqs.size()), reqs.data(), MPI_STATUSES_IGNORE);

    if (ack[0] != magic)
    {
        fail("handshake reply from Yade has a bad magic number");
    }
    if (ack[1] != protocolVersion)
    {
        std::ostringstream m;
        m << "protocol version mismatch: OpenFOAM pgfYadeCoupler v"
          << protocolVersion << ", Yade PgfCoupling v" << ack[1];
        fail(m.str());
    }
    if (ack[2] != 0)
    {
        static const char* why[] =
            {"ok", "protocol version", "per-particle stride", "lambdaMode",
             "magic number"};
        std::ostringstream m;
        m << "Yade rejected the handshake: "
          << (ack[2] > 0 && ack[2] < 5 ? why[ack[2]] : "unknown")
          << " mismatch (see Yade's log)";
        fail(m.str());
    }
    firstWorker_ = ack[3];
    nWorkers_ = ack[4];

    Info<< "pgfYadeCoupler: handshake ok, protocol v" << protocolVersion
        << ", lambdaMode " << lambdaMode_ << ", " << nWorkers_
        << " Yade worker(s)" << endl;
}


void pgfYadeCoupler::exchange(scalar deltaT)
{
    particles_.clear();

    std::vector<std::vector<double>> reply(nWorkers_);
    std::vector<int> counts(nWorkers_, 0);

    for (int w = 0; w < nWorkers_; ++w)
    {
        const int src = firstWorker_ + w;
        int n = 0;
        MPI_Recv(&n, 1, MPI_INT, src, TAG_COUNT, interComm_, MPI_STATUS_IGNORE);
        if (n <= 0) continue;

        std::vector<double> data(strideUp*n);
        MPI_Recv
        (
            data.data(), int(data.size()), MPI_DOUBLE, src, TAG_DATA,
            interComm_, MPI_STATUS_IGNORE
        );

        for (int i = 0; i < n; ++i)
        {
            const double* d = &data[strideUp*i];
            const label cellI = mesh_.findCell(point(d[1], d[2], d[3]));
            if (cellI < 0) continue;

            particles_.append
            (
                demParticle
                {
                    label(std::llround(d[0])), cellI,
                    vector(d[4], d[5], d[6]), d[7]
                }
            );
            reply[w].push_back(d[0]);
            reply[w].push_back(lambdaDot_[cellI]);
        }
        counts[w] = int(reply[w].size()/strideDown);
    }

    std::vector<MPI_Request> reqs;
    reqs.reserve(2*nWorkers_);
    for (int w = 0; w < nWorkers_; ++w)
    {
        const int dst = firstWorker_ + w;
        reqs.emplace_back();
        MPI_Isend(&counts[w], 1, MPI_INT, dst, TAG_REPLY_COUNT, interComm_, &reqs.back());
        if (counts[w] > 0)
        {
            reqs.emplace_back();
            MPI_Isend
            (
                reply[w].data(), int(reply[w].size()), MPI_DOUBLE, dst,
                TAG_REPLY, interComm_, &reqs.back()
            );
        }
    }
    MPI_Waitall(int(reqs.size()), reqs.data(), MPI_STATUSES_IGNORE);

    if (Pstream::master())
    {
        double dt = deltaT;
        MPI_Send(&dt, 1, MPI_DOUBLE, 0, TAG_FOAM_DT, interComm_);
    }
}

} // namespace Foam
