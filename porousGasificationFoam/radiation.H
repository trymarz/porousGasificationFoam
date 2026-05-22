/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright held by original author
     \\/     M anipulation  |
-------------------------------------------------------------------------------
Global
    radiation

Description
    Drive the heterogeneous radiation model and publish the resulting
    solid radiative source.

    Single step before the pyrolysis evolve. The runtime-selected
    \c heterogeneousRadiationModel (heterogeneousMeanTemp,
    heterogeneousP1 or heterogeneousNoRadiation) updates its internal
    state inside \c correct(), then exposes the volumetric radiative
    source for the solid energy equation via \c solidSh(). The result
    is copied into the field \c radiationF, which is read by
    \c volPyrolysis when assembling the solid energy equation. The
    field also feeds the gas-side radiation contribution
    \c radiation->Sh(thermo, he) used in EEqn.H.

    The active model and its frequency of update are configured in
    \c constant/radiationProperties (see README → Input File Reference).

\*---------------------------------------------------------------------------*/

radiation->correct();
radiationF = radiation->solidSh();
