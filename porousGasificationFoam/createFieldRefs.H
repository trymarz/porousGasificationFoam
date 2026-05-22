/*---------------------------------------------------------------------------*\
Global
    createFieldRefs

Description
    Convenience const-references into the thermophysical model that are
    used repeatedly in the main loop.

    \c psi = d(rho)/d(p) at constant entropy (compressibility), used by
    the pressure equations in pEqn.H / pcEqn.H. \c T is the gas
    temperature, read by setRDeltaT.H, EEqn.H diagnostics, and the
    pyrolysis heat-transfer terms.

\*---------------------------------------------------------------------------*/

const volScalarField& psi = thermo.psi();
const volScalarField& T = thermo.T();
