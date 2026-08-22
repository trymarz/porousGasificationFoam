/*  Minimal OF-side YADE coupling for porousGasificationFoam.
 *
 *  Derived from FoamYade.H (c) 2019 Deepak Kunhappan. Stripped to the two
 *  signals PGF actually uses:
 *    - YADE -> OF : particle pos/vel/radius (7 doubles/particle)
 *    - OF -> YADE : per-particle lambdaDot   (1 double/particle)
 *  No drag, no lift, no torques, no Gaussian interpolation, no cell volume
 *  fraction, no momentum source terms, no meshTree. Particle location uses
 *  mesh.findCell() directly.
 */

#ifndef PgfToYadeMpiCoupler_H
#define PgfToYadeMpiCoupler_H

#include <algorithm>
#include <map>
#include <memory>
#include <vector>

// Targets OpenFOAM.com v2406 only (no OF-foundation / .org branching).
#include "fvCFD.H"

#include <mpi.h>


namespace Foam {

class YadeParticleCoupledToPGF {
public:
	YadeParticleCoupledToPGF() {};
	double           dia;
	scalar           lambdaDot = 0.0;
	point            pos;
	vector           linearVelocity;
	std::vector<int> cellIds;
	label            inCell;
	int              indx;
	virtual ~YadeParticleCoupledToPGF() {};
};

class YadeProcToPgfCouplingPipeline {
public:
	YadeProcToPgfCouplingPipeline() {};
	int                                       yRank;            // world rank of the yade Proc.
	std::vector<int>                          numParticlesProc; // number of particles from intersections with yade proc
	bool                                      inComm = false;   // flag for intersection with this proc.
	std::vector<double>                       particleDataBuff; // buff to recv particle info: 7 doubles/particle (pos[3], vel[3], radius[1])
	std::vector<int>                          foundBuff;        // buff to send grid search results
	std::vector<double>                       lambdaDotBuff;    // buff to send lambdaDot: 1 double/particle
	std::vector<std::shared_ptr<YadeParticleCoupledToPGF>> foundParticles; // vector of found particles from this yadeProc.
	int                                                    numParticles;
	~YadeProcToPgfCouplingPipeline() {};
};

class PgfToYadeMpiCoupler {
protected:
	/* tags for MPI messages -- same integers as the legacy coupling; the two
	   backends are never active simultaneously in the same run. */
	const int TAG_SZ_BUFF    = 1003;
	const int TAG_GRID_BBOX  = 1001;
	const int TAG_YADE_DATA  = 1002;
	const int TAG_LAMBDA     = 1005; // OF -> YADE lambdaDot (same value as the legacy force tag)
	const int TAG_SEARCH_RES = 1004;
	const int TAG_FLUID_DT   = 1050;
	const int TAG_YADE_DT    = 1060;
	const int TAG_SHARED_ID  = 1080;

	MPI_Comm parentComm;
	MPI_Comm interComm;

public:
	std::vector<MPI_Request>              reqVec;
	int                                   localRank, worldRank;
	int                                   localCommSize, worldCommSize;
	int                                   commSzDff;
	bool                                  couplingIsInitialized = false;
	const fvMesh&                         mesh;
	volScalarField&                       lambdaDot;
	bool                                  serialYade = false;

	scalar                                deltaT;
	double                                yadeDT;
	std::vector<YadeProcToPgfCouplingPipeline>                  yadeProcs;
	std::vector<std::shared_ptr<YadeProcToPgfCouplingPipeline>> inCommProcs;

	PgfToYadeMpiCoupler(const fvMesh& _mesh, volScalarField& _lambdaDot)
	        : mesh(_mesh)
	        , lambdaDot(_lambdaDot)
	{
		initializeMpiConnectionWithYade();
	}

	void             initializeMpiConnectionWithYade();
	void             sendMeshBoundingBoxToYade();
	void             receiveParticleDataFromYadeProcs();
	void             locateReceivedParticlesOnFluidMesh();
	std::vector<int> locatePt(const vector& pt);
	void             sendLambdaDotToYadeProcs();
	void             syncTimestepsWithYade();
	void             exchangeParticleDataWithYade(double dt);
	void             clearParticleExchangeBuffers();
	void             finalizeRun();
	void             printMsg(const std::string& msg);

	virtual ~PgfToYadeMpiCoupler() {};
};

} // namespace Foam
#endif
