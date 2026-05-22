/*---------------------------------------------------------------------------*\
Global
    readChemistryTimeControls

Description
    Read the on/off switches that govern chemistry-driven time-step
    adjustment.

    Two switches are read from \c constant/chemistryProperties (both
    default to false):
      - \c gasChemistryTimeStepControl — currently parsed but not
        consumed by the solver loop. Reserved for future use; the gas
        chemistry timescale is normally swept up by the standard
        Courant limit.
      - \c solidChemistryTimeStepControl — when true, the heterogeneous
        chemistry timescale reported by the active pyrolysis model
        becomes a binding constraint on deltaT (see
        \c updateChemistryTimeStep.H).

\*---------------------------------------------------------------------------*/

#ifndef READCHEMISTRYTIMECONTROLS_H
#define READCHEMISTRYTIMECONTROLS_H

Switch gasChemistryTimeStepControl
(
    mesh.lookupObject<dictionary>
        ("chemistryProperties").lookupOrDefault("gasChemistryTimeStepControl", false)
);

Switch solidChemistryTimeStepControl
(
    mesh.lookupObject<dictionary>
        ("chemistryProperties").lookupOrDefault("solidChemistryTimeStepControl", false)
);

#endif // READCHEMISTRYTIMECONTROLS_H
