/*---------------------------------------------------------------------------*\
Description
    Compile-time selection of the OF-side YADE coupling backend.

    Two backends exist in the Foam-Yade repository and PGF can be built
    against either of them; which one is compiled in is decided by the
    YADE_COUPLING_LIB build variable (see build.sh), which defines
    YADE_COUPLING_PGF for the minimal backend:

      - foamYade (default, YADE_COUPLING_PGF undefined)
            FoamYade: full two-way coupling - drag/lift/torque feedback on
            the particles, a k-d-tree cell search (meshTree) and Gaussian
            volume-fraction interpolation.

      - pgfYade (YADE_COUPLING_PGF defined)
            PgfToYadeMpiCoupler: exchanges only particle kinematics
            (YADE -> PGF) and one lambdaDot per particle (PGF -> YADE).
            No forces, no torques, plain mesh.findCell() location.

    Both expose the same inCommProcs/foundParticles shape
    (.inCell, .indx, .linearVelocity, .lambdaDot), so everything that only
    reads particle data - lambdaDotModel in particular - is written against
    the Foam::DemYadeCoupler typedef and needs no branching.

    One binary carries exactly one backend; there is no run-time choice on
    the OF side. The YADE-side engine class chosen by the case's Python
    script must match what the solver was compiled with.

Note
    wmkdepend does not evaluate preprocessor conditionals, so every build
    prints a harmless "could not open" line for the header of the backend
    that is not selected. It affects dependency generation only.
\*---------------------------------------------------------------------------*/

#ifndef demCouplingLib_H
#define demCouplingLib_H

#if defined(YADE_COUPLING_PGF)
    #include "PgfToYadeMpiCoupler.H"
    namespace Foam { typedef PgfToYadeMpiCoupler DemYadeCoupler; }
#else
    #include "FoamYade.H"
    namespace Foam { typedef FoamYade DemYadeCoupler; }
#endif

#endif

// ************************************************************************* //
