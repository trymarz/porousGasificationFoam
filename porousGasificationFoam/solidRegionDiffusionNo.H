/*---------------------------------------------------------------------------*\
Global
    solidRegionDiffusionNo

Description
    Compute the current maximum solid-region diffusion number.

    \c DiNum = max over the mesh of \f$\alpha_s \Delta t / \Delta x^2\f$,
    where \f$\alpha_s = K / (\rho_s C_{p,s})\f$ uses the per-cell solid
    thermal conductivity, density and specific heat held by the
    pyrolysis model. Computed once per time step before
    \c setMultiRegionDeltaT.H, which uses it together with \c CoNum to
    pick the next deltaT.

\*---------------------------------------------------------------------------*/

scalar DiNum =  pyrolysis->solidRegionDiffNo();
