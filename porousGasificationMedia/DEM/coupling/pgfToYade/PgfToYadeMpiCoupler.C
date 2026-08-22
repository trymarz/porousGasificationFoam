/*  Minimal OF-side YADE coupling for porousGasificationFoam.
 *  Derived from FoamYade.C (c) 2019 Deepak Kunhappan. See PgfToYadeMpiCoupler.H. */

#include "PgfToYadeMpiCoupler.H"
#include "PstreamGlobals.H"
#include <mpi.h>
#include <string>

void Foam::PgfToYadeMpiCoupler::printMsg(const std::string& msg)
{
	std::cout << " Rank : " << worldRank << "  " << msg << std::endl;
}


void Foam::PgfToYadeMpiCoupler::initializeMpiConnectionWithYade()
{
	if (!couplingIsInitialized) {
		std::cout << "FOAM: Starting intialization (PGF coupling backend active) " << std::endl;
		// get local rank and size
		MPI_Comm_rank(PstreamGlobals::MPICommunicators_[0], &localRank);
		MPI_Comm_size(PstreamGlobals::MPICommunicators_[0], &localCommSize);

		/// Get the parent communicator..
		MPI_Comm_get_parent(&parentComm);
		std::cout << "FOAM: Parent communicator set." << std::endl;
		MPI_Intercomm_merge(parentComm, 1, &interComm);
		std::cout << "FOAM: intracommunicator has been made" << std::endl;
		// world comm (intra communicator size)
		MPI_Comm_size(interComm, &worldCommSize);

		// diff in comm size
		commSzDff             = abs(worldCommSize - localCommSize);
		couplingIsInitialized = true;

		std::cout << "[DEBUG][OF rank " << localRank << "] initializeMpiConnectionWithYade: localCommSize=" << localCommSize
		          << " worldCommSize=" << worldCommSize << " commSzDff=" << commSzDff << std::endl;

		if (commSzDff == 1) { serialYade = true; }

		if (!serialYade) {
			// alloc vector of yadeProcs, yade Master does not participate in communications.
			yadeProcs.resize(commSzDff - 1);
			// we do not receive from yade Master,
			for (unsigned int i = 0; i != yadeProcs.size(); ++i) {
				yadeProcs[i].yRank = (int)i + 1;
				yadeProcs[i].numParticlesProc.resize(localCommSize);
				std::fill(yadeProcs[i].numParticlesProc.begin(), yadeProcs[i].numParticlesProc.end(), -1);
			}
		} else {
			yadeProcs.resize(1);
			yadeProcs[0].yRank = 0;
			yadeProcs[0].numParticlesProc.resize(localCommSize);
			std::fill(yadeProcs[0].numParticlesProc.begin(), yadeProcs[0].numParticlesProc.end(), -1);
		}
		sendMeshBoundingBoxToYade();
	}
}


void Foam::PgfToYadeMpiCoupler::sendMeshBoundingBoxToYade()
{
	//send mesh bbox from point field, min
	if (!couplingIsInitialized) {
		std::cerr << " Coupling has not been intialized. " << std::endl;
		return;
	}
	// get the mesh pointfield (cell vertices),

	point minBound(1e+50, 1e+50, 1e+50);
	point maxBound(-1e+50, -1e+50, -1e+50);


	for (const auto& pt : mesh.points()) {
		minBound.x() = Foam::min(pt.x(), minBound.x());
		minBound.y() = Foam::min(pt.y(), minBound.y());
		minBound.z() = Foam::min(pt.z(), minBound.z());

		maxBound.x() = Foam::max(pt.x(), maxBound.x());
		maxBound.y() = Foam::max(pt.y(), maxBound.y());
		maxBound.z() = Foam::max(pt.z(), maxBound.z());
	}

	std::vector<double> meshBbox = {minBound.x(), minBound.y(), minBound.z(), maxBound.x(), maxBound.y(), maxBound.z()};

	// send bounding box to every yade proc including yade Master.
	for (int rnk = 0; rnk != commSzDff; ++rnk) {
		MPI_Request req;
		MPI_Isend(&meshBbox.front(), 6, MPI_DOUBLE, rnk, TAG_GRID_BBOX, interComm, &req);
		reqVec.push_back(req);
	}

	for (auto rq : reqVec) {
		MPI_Status status;
		MPI_Wait(&rq, &status);
	}

	reqVec.clear();
}


void Foam::PgfToYadeMpiCoupler::receiveParticleDataFromYadeProcs()
{
	for (auto& yProc : yadeProcs) {
		std::cout << "[DEBUG][OF rank " << localRank << "] recv TAG_SZ_BUFF from yRank=" << yProc.yRank << " ..." << std::endl;
		MPI_Status status;
		MPI_Recv(&yProc.numParticlesProc.front(), localCommSize, MPI_INT, yProc.yRank, TAG_SZ_BUFF, interComm, &status);
		std::cout << "[DEBUG][OF rank " << localRank << "] got TAG_SZ_BUFF from yRank=" << yProc.yRank
		          << " numParticlesProc[localRank]=" << yProc.numParticlesProc[localRank] << std::endl;
	}

	// those yade procs intersecting current grid.
	for (auto& yProc : yadeProcs) {
		if (yProc.numParticlesProc[localRank] > 0) {
			yProc.numParticles = yProc.numParticlesProc[localRank];

			// 7 doubles/particle: pos[3], vel[3], radius[1]
			yProc.particleDataBuff.resize(yProc.numParticles * 7);

			yProc.foundBuff.resize(yProc.numParticles);     // grid search result, 1 int/particle
			yProc.lambdaDotBuff.resize(yProc.numParticles); // lambdaDot, 1 double/particle

			std::fill(yProc.foundBuff.begin(), yProc.foundBuff.end(), -1);
			std::fill(yProc.lambdaDotBuff.begin(), yProc.lambdaDotBuff.end(), 0.0);
			inCommProcs.push_back(std::make_shared<YadeProcToPgfCouplingPipeline>(yProc)); // keep list of yadeProcs with intersection.
		}
	}


	// get the data from intersecting procs.
	for (auto& yProc : inCommProcs) {
		std::cout << "[DEBUG][OF rank " << localRank << "] recv TAG_YADE_DATA from yRank=" << yProc->yRank
		          << " sz=" << yProc->particleDataBuff.size() << " ..." << std::endl;
		MPI_Status           status;
		std::vector<double>& dBuff = yProc->particleDataBuff;
		MPI_Recv(&dBuff.front(), (int)dBuff.size(), MPI_DOUBLE, yProc->yRank, TAG_YADE_DATA, interComm, &status);
		std::cout << "[DEBUG][OF rank " << localRank << "] got TAG_YADE_DATA from yRank=" << yProc->yRank << std::endl;
	}
}


void Foam::PgfToYadeMpiCoupler::locateReceivedParticlesOnFluidMesh()
{
	for (const auto& yProc : inCommProcs) {
		for (int np = 0; np != yProc->numParticles; ++np) {
			vector pos(0, 0, 0);
			pos.x() = yProc->particleDataBuff[np * 7];
			pos.y() = yProc->particleDataBuff[np * 7 + 1];
			pos.z() = yProc->particleDataBuff[np * 7 + 2];

			std::vector<int> cellIds = locatePt(pos);

			if (cellIds.size() && cellIds[0] > -1) {
				std::shared_ptr<YadeParticleCoupledToPGF> yParticle = std::make_shared<YadeParticleCoupledToPGF>();
				yProc->inComm                          = true;
				yParticle->pos                         = pos;
				yParticle->cellIds                     = cellIds;
				yParticle->inCell                      = cellIds[0];
				yParticle->indx                        = np; // keep track of 'found particle' index

				yParticle->linearVelocity.x() = yProc->particleDataBuff[np * 7 + 3];
				yParticle->linearVelocity.y() = yProc->particleDataBuff[np * 7 + 4];
				yParticle->linearVelocity.z() = yProc->particleDataBuff[np * 7 + 5];

				yParticle->dia = 2 * yProc->particleDataBuff[np * 7 + 6];

				yProc->foundBuff[np] = 1;

				yProc->foundParticles.push_back(yParticle);
			}
		}
		yProc->particleDataBuff.clear();
	}

	// send the 'foundBuff' of intersecting procs to Yade.
	for (const auto& yProc : inCommProcs) {
		std::cout << "[DEBUG][OF rank " << localRank << "] send TAG_SEARCH_RES to yRank=" << yProc->yRank << " ..." << std::endl;
		std::vector<int>& fBuff = yProc->foundBuff;
		int               sz    = int(fBuff.size());
		MPI_Send(&fBuff.front(), sz, MPI_INT, yProc->yRank, TAG_SEARCH_RES, interComm);
		std::cout << "[DEBUG][OF rank " << localRank << "] sent TAG_SEARCH_RES to yRank=" << yProc->yRank << std::endl;
	}

	//recv info of 'shared particles'.
	std::vector<int> sharedCount;
	std::vector<int> rnks;
	sharedCount.resize(inCommProcs.size());

	int n = 0;
	for (const auto& yProc : inCommProcs) {
		std::cout << "[DEBUG][OF rank " << localRank << "] recv TAG_SHARED_ID (count) from yRank=" << yProc->yRank << " ..." << std::endl;
		int&       ncount = sharedCount[n];
		MPI_Status stat;
		MPI_Recv(&ncount, 1, MPI_INT, yProc->yRank, TAG_SHARED_ID, interComm, &stat);
		std::cout << "[DEBUG][OF rank " << localRank << "] got TAG_SHARED_ID count=" << ncount << " from yRank=" << yProc->yRank << std::endl;
		if (ncount > 0) rnks.push_back(n);
		++n;
	}


	std::vector<std::vector<std::vector<int>>> sharedBuff;
	sharedBuff.resize(inCommProcs.size());
	// prealloc some arrays in sharedBuff;
	for (unsigned i = 0; i != sharedCount.size(); ++i) {
		if (sharedCount[i] > 0) sharedBuff[i].resize(sharedCount[i]);
	}


	if (rnks.size()) {
		for (unsigned i = 0; i != rnks.size(); ++i) {
			const int& sender = inCommProcs[rnks[i]]->yRank;
			const int& ncount = sharedCount[rnks[i]];
			if (ncount == 0) continue;
			for (int ii = 0; ii != ncount; ++ii) {
				std::vector<int> buff;
				MPI_Status       status;
				MPI_Probe(sender, TAG_SHARED_ID, interComm, &status);
				int sz;
				MPI_Get_count(&status, MPI_INT, &sz);
				buff.resize(sz);
				MPI_Recv(&buff.front(), sz, MPI_INT, sender, TAG_SHARED_ID, interComm, &status);
				sharedBuff[rnks[i]][ii] = buff;
			}
		}
	}
}

std::vector<int> Foam::PgfToYadeMpiCoupler::locatePt(const vector& pt)
{
	std::vector<int> cellId;
	int              inCell = mesh.findCell(pt);
	if (inCell > -1) cellId.push_back(inCell);
	return cellId;
}


void Foam::PgfToYadeMpiCoupler::sendLambdaDotToYadeProcs()
{
	// pack 1 double (lambdaDot) per found particle from its cell value
	for (auto& yProc : inCommProcs) {
		for (auto& prt : yProc->foundParticles) {
			scalar      ld    = 0.0;
			const label cellI = prt->inCell;
			if (cellI >= 0 && cellI < lambdaDot.size()) ld = lambdaDot[cellI];
			prt->lambdaDot              = ld;
			yProc->lambdaDotBuff[prt->indx] = ld;
		}
	}

	// send full buffer (numParticles doubles) to each intersecting yade proc
	for (auto& yProc : inCommProcs) {
		std::cout << "[DEBUG][OF rank " << localRank << "] send TAG_LAMBDA to yRank=" << yProc->yRank << " ..." << std::endl;
		MPI_Send(&yProc->lambdaDotBuff.front(), yProc->numParticles, MPI_DOUBLE, yProc->yRank, TAG_LAMBDA, interComm);
		std::cout << "[DEBUG][OF rank " << localRank << "] sent TAG_LAMBDA to yRank=" << yProc->yRank << std::endl;
	}
}


void Foam::PgfToYadeMpiCoupler::syncTimestepsWithYade()
{
	std::cout << "[DEBUG][OF rank " << localRank << "] syncTimestepsWithYade: serialYade=" << serialYade << " ..." << std::endl;
	if (localRank == 0) {
		MPI_Send(&deltaT, 1, MPI_DOUBLE, 0, TAG_FLUID_DT, interComm);
		std::cout << "[DEBUG][OF rank " << localRank << "] sent TAG_FLUID_DT" << std::endl;
	}
	if (!serialYade) {
		if (localRank == 0) {
			std::cout << "[DEBUG][OF rank " << localRank << "] recv TAG_YADE_DT ..." << std::endl;
			MPI_Status status;
			MPI_Recv(&yadeDT, 1, MPI_DOUBLE, 0, TAG_YADE_DT, interComm, &status);
			std::cout << "[DEBUG][OF rank " << localRank << "] got TAG_YADE_DT=" << yadeDT << std::endl;
		}
		// broadcast recvd yadeDt from localRank = 0.
		std::cout << "[DEBUG][OF rank " << localRank << "] bcast yadeDT on local comm ..." << std::endl;
		MPI_Bcast(&yadeDT, 1, MPI_DOUBLE, 0, PstreamGlobals::MPICommunicators_[0]);
		std::cout << "[DEBUG][OF rank " << localRank << "] bcast yadeDT done" << std::endl;
	} else {
		MPI_Bcast(&yadeDT, 1, MPI_DOUBLE, 0, interComm);
	}
}


void Foam::PgfToYadeMpiCoupler::clearParticleExchangeBuffers()
{
	if (inCommProcs.size()) {
		for (const auto& yp : inCommProcs) {
			yp->foundBuff.clear();
			yp->foundParticles.clear();
			yp->lambdaDotBuff.clear();
		}
		inCommProcs.clear();
	}
}


void Foam::PgfToYadeMpiCoupler::finalizeRun()
{
	int value = -1;
	MPI_Bcast(&value, 1, MPI_INT, 0, interComm);
	if (value == 10) MPI_Finalize();
}


/* main driver */
void Foam::PgfToYadeMpiCoupler::exchangeParticleDataWithYade(double dt)
{
	deltaT = dt;
	std::cout << "[DEBUG][OF rank " << localRank << "] exchangeParticleDataWithYade: START dt=" << dt << std::endl;
	receiveParticleDataFromYadeProcs();
	std::cout << "[DEBUG][OF rank " << localRank << "] exchangeParticleDataWithYade: receiveParticleDataFromYadeProcs DONE" << std::endl;
	locateReceivedParticlesOnFluidMesh();
	std::cout << "[DEBUG][OF rank " << localRank << "] exchangeParticleDataWithYade: locateReceivedParticlesOnFluidMesh DONE" << std::endl;
	sendLambdaDotToYadeProcs();
	std::cout << "[DEBUG][OF rank " << localRank << "] exchangeParticleDataWithYade: sendLambdaDotToYadeProcs DONE" << std::endl;
	syncTimestepsWithYade();
	std::cout << "[DEBUG][OF rank " << localRank << "] exchangeParticleDataWithYade: END" << std::endl;
}
