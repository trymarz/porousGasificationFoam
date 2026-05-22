/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright held by original author
     \\/     M anipulation  |
-------------------------------------------------------------------------------
Global
    updateChemistryTimeStep

Description
    Further shrink the global deltaT to respect the heterogeneous
    chemistry timescale.

    Layered on top of \c setMultiRegionDeltaT.H. When
    \c solidChemistryTimeStepControl is enabled in
    \c constant/chemistryProperties, the maximum stable step set by
    Courant/diffusion is compared against the chemistry-limited step
    reported by \c pyrolysisZone.maxTime() (a function of the current
    reaction rate field), and the smaller of the two is used.

    A diagnostic line is printed when the chemistry timescale becomes
    the binding constraint — useful for telling whether a slow run is
    chemistry- or CFL-limited.

\*---------------------------------------------------------------------------*/

if (adjustTimeStep and solidChemistryTimeStepControl)
{

    const scalar TsolidChemistry = pyrolysisZone.maxTime();
    const scalar dt0 = runTime.deltaTValue();

    if (min(TsolidChemistry,dt0) == TsolidChemistry)
    {
        Info << "timestep corrected for heterogeneous reactions to: " << TsolidChemistry << "[s]" << endl;
    }

    runTime.setDeltaT
    (
        min
        (
            TsolidChemistry,
            dt0
        )
    );

}
