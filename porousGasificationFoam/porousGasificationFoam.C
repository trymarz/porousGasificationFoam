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
    Solver for reactive flow through porous medium

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
#include "FoamYade.H"
#include "lambdaDotModel.H"

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
    #include "createFieldRefs.H"
    #include "createPorosity.H"
    #include "createPyrolysisModel.H"
    #include "readPyrolysisTimeControls.H"
    #include "createHeterogeneousRadiationModel.H"
    #include "readChemistryTimeControls.H"

    if (DEM)
    {
        bool gaussianInterp = false;

        FoamYade yadeCoupling(mesh,U, gradP, vGrad, divT, ddtU_f, g, uSourceDrag, alphac, uSource, uParticle, uCoeff, uInterp, lambdaDot, gaussianInterp);

        yadeCoupling.setScalarProperties(partDensity.value(), fluidDensity.value(), nu.value());
        Info<< "Particles properties set." << endl;

        lambdaDotModel lambdaDotUpdater(mesh, lambdaDot, nParticles, Us, UsInterp, porosityF, yadeCoupling);

        yadeCoupling.locateAllParticles();   // places the location of spheres from time 0
    }


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

        if (DEM)
        {
            vGrad = fvc::grad(U);
            yadeCoupling.setParticleAction(runTime.deltaT().value());
            lambdaDotUpdater.update(); //DasteXar jadid
            lambdaDotUpdater.writeParticlesData(); // DasteXar to write ParticlesData.txt in each time step for each rank
            yadeCoupling.setSourceZero();
        }

        #include "radiation.H"
        pyrolysisZone.evolve();

        #include "rhoEqn.H"

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

            // --- Pressure corrector loop
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
