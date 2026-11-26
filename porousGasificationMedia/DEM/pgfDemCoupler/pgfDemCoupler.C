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

#include "pgfDemCoupler.H"
#include <limits>

namespace Foam
{

//- This rank's mesh-point bounds; mesh.bounds() is reduced over all ranks.
static PgfDemBridge::Box localBox(const fvMesh& mesh)
{
    point lo(GREAT, GREAT, GREAT);
    point hi(-GREAT, -GREAT, -GREAT);
    for (const point& p : mesh.points())
    {
        lo = min(lo, p);
        hi = max(hi, p);
    }
    return {{lo.x(), lo.y(), lo.z()}, {hi.x(), hi.y(), hi.z()}};
}


pgfDemCoupler::pgfDemCoupler
(
    const fvMesh& mesh,
    const volScalarField& lambdaDot,
    PgfDemBridge::LambdaMode lambdaMode
)
:
    mesh_(mesh),
    lambdaDot_(lambdaDot)
{
    // The bridge needs MPI, which OpenFOAM starts only with -parallel.
    if (!Pstream::parRun())
    {
        FatalErrorInFunction
            << "DEM coupling needs -parallel (for one OpenFOAM rank, a"
            << " one-piece decomposition in processor0)" << exit(FatalError);
    }

    side_.reset
    (
        new PgfDemBridge::FoamSide(lambdaMode, localBox(mesh), MPI_COMM_WORLD)
    );

    Info<< "pgfDemCoupler: handshake ok, protocol v"
        << PgfDemBridge::protocolVersion << ", lambdaMode "
        << int(lambdaMode) << ", " << side_->nDem() << " DEM rank(s)" << endl;
}


pgfDemCoupler::~pgfDemCoupler() = default;


bool pgfDemCoupler::exchange(scalar deltaT, bool lastStep)
{
    particles_.clear();

    const std::vector<double>& up = side_->receive();
    if (side_->stopped())
    {
        return false;
    }

    constexpr int su = PgfDemBridge::strideUp;
    const std::size_t n = side_->nBodies();
    values_.assign(n, std::numeric_limits<double>::quiet_NaN());

    for (std::size_t i = 0; i < n; ++i)
    {
        const double* r = &up[su*i];
        const label cellI = mesh_.findCell(point(r[0], r[1], r[2]));
        if (cellI < 0) continue;

        particles_.append(demParticle{cellI, vector(r[3], r[4], r[5]), r[6]});
        values_[i] = lambdaDot_[cellI];
    }

    side_->reply(values_.data(), deltaT, lastStep);
    return true;
}


void pgfDemCoupler::close()
{
    side_->close();
}

} // namespace Foam
