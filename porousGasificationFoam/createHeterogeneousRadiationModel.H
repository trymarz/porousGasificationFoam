/*---------------------------------------------------------------------------*\
Global
    createHeterogeneousRadiationModel

Description
    Runtime-select the heterogeneous radiation model that drives the
    solid radiative source field.

    The model is chosen from \c constant/radiationProperties (key
    \c heterogeneousRadiationModel). Options are:
      - \c heterogeneousNoRadiation: identity (zero source);
      - \c heterogeneousMeanTemp:    mean-temperature surface-only
                                     model (cheaper, suitable for thin
                                     beds);
      - \c heterogeneousP1:          P1 diffusion approximation
                                     (full volumetric, more expensive).

    Four references are passed in: gas temperature \c thermo.T(),
    porosity \c porosityF, the surface-cell mask \c pyrolysisZone.surf()
    (identifying border cells inside the solid body), and solid
    temperature \c pyrolysisZone.T(). The model is driven from
    \c radiation.H once per time step.

\*---------------------------------------------------------------------------*/

autoPtr<heterogeneousRadiationModel> radiation
(
    heterogeneousRadiationModel::New(thermo.T(),porosityF,pyrolysisZone.surf(),pyrolysisZone.T())
);
