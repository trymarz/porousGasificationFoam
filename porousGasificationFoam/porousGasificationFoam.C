/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     | Website:  https://openfoam.org
    \\  /    A nd           | Copyright (C) 2011-2018 OpenFOAM Foundation
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

Application
    porousGasificationFoam

Description
    Transient, compressible, reactive PIMPLE solver for a gas flowing
    through a reactive porous medium. The solid phase is represented by a
    cell-centred porosity field \c porosityF in [0,1] (1 = pure gas,
    0 = pure solid). Solid and gas coexist in every cell and exchange
    mass, momentum and energy through coupling source terms supplied by
    the heterogeneous pyrolysis / chemistry / radiation models.

    Per-time-step structure (top-to-bottom in main()):
      1. Time-step control: combine fluid Courant, solid diffusion and
         chemistry timescales into a single stable deltaT.
         See \c setMultiRegionDeltaT.H and \c updateChemistryTimeStep.H.
      2. Optional DEM coupling (when WITH_YADE is defined): exchange
         particle state with YADE and build the smoothed solid velocity
         field. See \c lambdaDotModel::update().
      3. Heterogeneous radiation: update the solid radiative source.
         See \c radiation.H and \c heterogeneousRadiationModel.
      4. Solid-phase evolution: per-cell chemistry ODE, solid species
         mass conservation, porosity update (optional bed collapse) and
         the solid energy equation. See \c volPyrolysis::evolveRegion().
      5. Gas continuity with the solid-to-gas mass source.
         See \c rhoEqn.H.
      6. PIMPLE loop: momentum (UEqn.H) with Darcy/Forchheimer porous
         resistance, gas species (YEqn.H), gas enthalpy (EEqn.H) and
         pressure correction (pEqn.H or pcEqn.H depending on whether
         pimple.consistent() is set).
      7. Turbulence correction.

    Geometry, solid material, reaction set, radiation parameters and
    porous resistance tensor are all user inputs. See README Part I for
    the input-file reference.

\*---------------------------------------------------------------------------*/

#include "fvCFD.H"
#include "turbulentFluidThermoModel.H"
#include "psiReactionThermo.H"
#include "CombustionModel.H"
#include "multivariateScheme.H"
#include "pimpleControl.H"
#include "pressureControl.H"
#include "fvOptions.H"
#include "localEulerDdtScheme.H"
#include "fvcSmooth.H"
#include "fieldPorosityModel.H"
#include "porousThermoSolidChemistryModel.H"
#include "heterogeneousPyrolysisModel.H"
#include "heterogeneousRadiationModel.H"
#include "HGSSolidThermo.H"

#ifdef WITH_YADE 
    #include "FoamYade.H"
    #include "lambdaDotModel.H"
#endif // WITH_YADE 


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //
int main(int argc, char *argv[])
{
    #include "postProcess.H"
    #include "setRootCaseLists.H"
    #include "createTime.H"
    #include "createMesh.H"
    #include "createControl.H"
    #include "createTimeControls.H"
    #include "initContinuityErrs.H"
    #include "createFields.H"

    #ifdef WITH_YADE
        #include "createDEMFields.H"
    #endif // WITH_YADE

    #include "createFieldRefs.H"
    #include "createPorosity.H"
    #include "createPyrolysisModel.H"
    #include "readPyrolysisTimeControls.H"
    #include "createHeterogeneousRadiationModel.H"
    #include "readChemistryTimeControls.H"
    #ifdef WITH_YADE
        #include "createYadeCoupling.H"
    #endif // WITH_YADE

    turbulence->validate();
    if (!LTS)
    {
        #include "compressibleCourantNo.H"
        #include "setInitialDeltaT.H"
    }

    // * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

    Info<< "\nStarting time loop\n" << endl;

    while (runTime.run())
    {
        #include "readTimeControls.H"

        // --- Time-step control.
        //     Either local-time-stepping (steady-state-like) or a global
        //     deltaT chosen as the minimum of the fluid Courant, solid
        //     diffusion and chemistry timescales.
        if (LTS)
        {
            #include "setRDeltaT.H"
        }
        else
        {
            #include "compressibleCourantNo.H"
            #include "solidRegionDiffusionNo.H"
            #include "setMultiRegionDeltaT.H"
            #include "updateChemistryTimeStep.H"

            Info<< "deltaT = " <<  runTime.deltaT().value() << endl;
        }

        runTime++;

        Info<< "Time = " << runTime.timeName() << nl << endl;

        #ifdef WITH_YADE
        // --- DEM coupling step (only when built with WITH_YADE and the
        //     yadeProperties dictionary has active = true).
        //     Pushes the current gas state to YADE, advances particles,
        //     pulls back per-cell aggregates, and produces the smoothed
        //     solid velocity field Us via lambdaDotModel::update().
        if (DEM)
        {
            vGrad = fvc::grad(U);
            yadeCoupling->setParticleAction(runTime.deltaT().value());
            lambdaDotUpdater->update();
            lambdaDotUpdater->writeParticlesData();
            yadeCoupling->setSourceZero();
        }
        #endif

        // --- Heterogeneous radiation source for the solid energy
        //     equation. The active model (heterogeneousP1 /
        //     heterogeneousMeanTemp / heterogeneousNoRadiation) is
        //     selected at runtime from constant/radiationProperties.
        #include "radiation.H"

        // --- Solid-phase evolution: heterogeneous chemistry ODE,
        //     solid species mass conservation, porosity evolution
        //     (with optional bed collapse) and the solid energy
        //     equation. The single line below drives steps (4) of the
        //     per-time-step tour; see volPyrolysis::evolveRegion().
        pyrolysisZone.evolve();

        // --- Gas continuity with the solid-to-gas mass source from
        //     pyrolysisZone (Srho is set in rhoEqn.H from
        //     pyrolysisZone.Srho()).
        #include "rhoEqn.H"

        // --- PIMPLE pressure-velocity coupling.
        //     Momentum predictor (with Darcy/Forchheimer porous
        //     resistance), gas species, gas enthalpy, then one or more
        //     pressure corrector iterations.
        while (pimple.loop())
        {
            if (pimple.nCorrPIMPLE() > 0)
            {
                p.storePrevIter();
                rho.storePrevIter();
                U.storePrevIter();
            }

            #include "UEqn.H"
            #include "YEqn.H"
            #include "EEqn.H"

            // --- Pressure corrector loop. Two variants of the pressure
            //     equation: the "consistent" SIMPLEC-style form
            //     (pcEqn.H) and the standard PISO/PIMPLE form (pEqn.H).
            while (pimple.correct())
            {
                if (pimple.consistent())
                {
                    #include "pcEqn.H"
                }
                else
                {
                    #include "pEqn.H"
                }
            }
        }

        if (pimple.turbCorr())
        {
            turbulence->correct();
        }

        rho = thermo.rho();

        Info<< "rho max/min : " << max(rho).value()
            << " " << min(rho).value() << endl;

        runTime.write();

        Info<< "ExecutionTime = " << runTime.elapsedCpuTime() << " s"
            << "  ClockTime = " << runTime.elapsedClockTime() << " s"
            << nl << endl;
    }

    Info<< "End\n" << endl;

    return 0;
}


// ************************************************************************* //
