/*---------------------------------------------------------------------------*\
Description
    Compile-time check of the Foam-Yade coupling API (#257). Foam-Yade's
    FoamYade.H defines FOAMYADE_COUPLING_API; a trunk built from another
    revision then fails here by name instead of with a constructor-arity
    error. Trunks older than the macro are not caught; pgf and build.sh
    compare the coupling-header stamp for those.
\*---------------------------------------------------------------------------*/

#ifndef foamYadeApiCheck_H
#define foamYadeApiCheck_H

#include "FoamYade.H"

//- The FoamYade constructor and buffer layout this tree is written against.
#define PGF_FOAMYADE_API_REQUIRED 1

#ifdef FOAMYADE_COUPLING_API
static_assert
(
    FOAMYADE_COUPLING_API == PGF_FOAMYADE_API_REQUIRED,
    "Foam-Yade coupling API differs from what porousGasificationFoam needs "
    "(PGF_FOAMYADE_API_REQUIRED): point YADE_TRUNK at the submodules/Foam-Yade "
    "pin and rebuild the coupling libs (utilities/pgf/buildCoupling.sh)"
);
#endif

#endif
