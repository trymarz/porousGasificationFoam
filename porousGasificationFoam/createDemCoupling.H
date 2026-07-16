autoPtr<PgfToYadeMpiCoupler> pgfToYadeCoupler(nullptr);
autoPtr<DemToFvmMapper> demToFvmMapper(nullptr);

if (demActive)
{
    // constructing the coupler initializes the MPI connection with the
    // spawning YADE process and sends this rank's mesh bounding box
    pgfToYadeCoupler.reset(new PgfToYadeMpiCoupler(mesh, lambdaDot));

    demToFvmMapper.reset(new DemToFvmMapper(
        mesh, lambdaDot, nParticles, UsDEM, Us, porosityF, pgfToYadeCoupler()));

    Info << "PGF-YADE coupling initialized." << endl;
}
