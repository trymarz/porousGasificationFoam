/*---------------------------------------------------------------------------*\
Global
    readPyrolysisTimeControls

Description
    Read the solid-side diffusion-number limit from the pyrolysis model.

    \c maxDi is the maximum solid-region diffusion number
    \f$\alpha_s \Delta t / \Delta x^2\f$ allowed when \c adjustTimeStep
    is on. Its value lives in the PIMPLE sub-dict / controlDict and is
    parsed by the pyrolysis model's read() routine; this header just
    publishes it into the main solver scope so \c setMultiRegionDeltaT.H
    can use it.

\*---------------------------------------------------------------------------*/

scalar maxDi = pyrolysis->maxDiff();
