/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright held by original author
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
    solidUsPotential

Description
    Static-IC utility for tutorials/cases/LEIgasifier. Solves a Poisson
    equation for a solid mass-flux potential Phi,

        fvm::laplacian(1, Phi) == S

    where S [kg/m^3/s] is a per-zone volumetric solid-to-gas mass sink read
    from constant/solidUsPotentialDict (three reaction zones: pyrolysis,
    combustion, gasification -- see lei-gasifier-continuity-us-field-2026-
    09-14.md Decisions 2-5). Phi is dimensioned so that the solid mass flux
    is exactly -grad(Phi) [kg/(m^2 s)]; no separate diffusivity term is
    needed. Boundary conditions:
      - inlet:  fixedGradient, sized so integrating the prescribed normal
                gradient over the inlet patch reproduces
                solidUsPotentialDict's inletMassFlux exactly.
      - wall, nozzle: zeroGradient (impermeable to the solid phase; the
                nozzle carries only gas).
      - outlet: fixedValue 0 -- a Dirichlet reference, not zeroGradient.
                A pure-Neumann system would force the sink's total to
                match the inlet flux exactly, which the deliberate ~90%
                conversion target (constant/solidUsPotentialDict) does not
                satisfy. Pinning the outlet lets its net outflow fall out
                of the solve as (inlet flux - integral(S)), which is the
                whole point of the sink-based reframing (see this plan's
                Context section) -- not something a zeroGradient outlet
                could deliver here.
      - sides:  empty (2D case).

    Us is then recovered as

        Us = -grad(Phi) / (rhoSolidLocal * (1 - porosityF))

    where rhoSolidLocal is the local solid mixture's true density, mixed
    from constant/solidThermophysicalProperties's per-component
    (rho_i, true) via the reciprocal rule 1/rho = sum_i(Y_i/rho_i) that
    volPyrolysis.H documents for Ym_i = Yi*rho_s*(1-porosity). Run once,
    before the case starts, on a staged copy of the case with 0/ already
    populated by setFields (porosityF and the Y<component> fields must be
    the post-setFields bed values, not the 0.orig uniform gas defaults);
    the resulting 0/Us is copied back into 0.orig/Us by hand. This does not
    change how the solver treats Us at run time -- it is not solved for
    again once the case starts.
\*---------------------------------------------------------------------------*/

#include "fvCFD.H"
#include "fixedGradientFvPatchFields.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

int main(int argc, char *argv[])
{
    #include "setRootCase.H"
    #include "createTime.H"
    #include "createMesh.H"

    Info<< "Reading solidUsPotentialDict\n" << endl;
    IOdictionary potentialDict
    (
        IOobject
        (
            "solidUsPotentialDict",
            runTime.constant(),
            mesh,
            IOobject::MUST_READ,
            IOobject::NO_WRITE
        )
    );

    const scalar inletMassFlux(readScalar(potentialDict.lookup("inletMassFlux")));
    const dictionary& zonesDict = potentialDict.subDict("zones");
    const wordList zoneNames(zonesDict.toc());

    Info<< "Reading solidThermophysicalProperties\n" << endl;
    IOdictionary solidThermoDict
    (
        IOobject
        (
            "solidThermophysicalProperties",
            runTime.constant(),
            mesh,
            IOobject::MUST_READ,
            IOobject::NO_WRITE
        )
    );
    const wordList solidComponents(solidThermoDict.lookup("solidComponents"));

    // Per-component true density [kg/m^3], read once.
    scalarField rhoTrue(solidComponents.size());
    forAll(solidComponents, i)
    {
        const dictionary& coeffs =
            solidThermoDict.subDict(solidComponents[i] + "Coeffs");
        rhoTrue[i] =
            readScalar(coeffs.subDict("density").lookup("rho"));
    }

    Info<< "Reading field porosityF\n" << endl;
    volScalarField porosityF
    (
        IOobject
        (
            "porosityF",
            runTime.timeName(),
            mesh,
            IOobject::MUST_READ,
            IOobject::NO_WRITE
        ),
        mesh
    );

    Info<< "Reading solid composition fields (Y<component>)\n" << endl;
    PtrList<volScalarField> Ys(solidComponents.size());
    forAll(solidComponents, i)
    {
        Ys.set
        (
            i,
            new volScalarField
            (
                IOobject
                (
                    "Y" + solidComponents[i],
                    runTime.timeName(),
                    mesh,
                    IOobject::MUST_READ,
                    IOobject::NO_WRITE
                ),
                mesh
            )
        );
    }

    // Local solid mixture true density: 1/rho = sum_i(Y_i / rho_i,true).
    Info<< "Computing the local solid mixture density\n" << endl;
    volScalarField invRhoSolid
    (
        IOobject
        (
            "invRhoSolid",
            runTime.timeName(),
            mesh,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh,
        dimensionedScalar(dimless/dimDensity, 0.0)
    );

    forAll(solidComponents, i)
    {
        invRhoSolid += Ys[i] / dimensionedScalar(dimDensity, rhoTrue[i]);
    }

    volScalarField rhoSolid
    (
        IOobject
        (
            "rhoSolid",
            runTime.timeName(),
            mesh,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        1.0 / max(invRhoSolid, dimensionedScalar(invRhoSolid.dimensions(), SMALL))
    );

    // Per-zone volumetric sink S [kg/m^3/s], uniform within each zone and
    // sized so that integrating S over the zone's own cell volume
    // reproduces solidUsPotentialDict's sinkMassRate exactly, regardless
    // of how many cells fall in the zone.
    Info<< "Building the per-zone sink field S\n" << endl;
    volScalarField S
    (
        IOobject
        (
            "S",
            runTime.timeName(),
            mesh,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh,
        dimensionedScalar(dimDensity/dimTime, 0.0)
    );

    const volVectorField& C = mesh.C();
    const scalar zEps = 1e-9;

    forAll(zoneNames, zi)
    {
        const dictionary& zoneDict = zonesDict.subDict(zoneNames[zi]);
        const scalar zMin(readScalar(zoneDict.lookup("zMin")));
        const scalar zMax(readScalar(zoneDict.lookup("zMax")));
        const scalar sinkMassRate(readScalar(zoneDict.lookup("sinkMassRate")));

        scalar zoneVolume = 0.0;
        forAll(C, celli)
        {
            if (C[celli].z() >= zMin - zEps && C[celli].z() < zMax - zEps)
            {
                zoneVolume += mesh.V()[celli];
            }
        }
        reduce(zoneVolume, sumOp<scalar>());

        if (zoneVolume < SMALL)
        {
            FatalErrorInFunction
                << "Zone " << zoneNames[zi] << " (z in [" << zMin << ", "
                << zMax << ")) contains no cells -- check the z-ranges in "
                << "constant/solidUsPotentialDict against the current mesh."
                << exit(FatalError);
        }

        const scalar sinkDensity = sinkMassRate/zoneVolume;

        Info<< "  zone " << zoneNames[zi] << ": z in [" << zMin << ", "
            << zMax << "), volume = " << zoneVolume << " m^3, sink = "
            << sinkMassRate << " kg/s -> " << sinkDensity << " kg/m^3/s"
            << endl;

        forAll(C, celli)
        {
            if (C[celli].z() >= zMin - zEps && C[celli].z() < zMax - zEps)
            {
                S[celli] = sinkDensity;
            }
        }
    }

    // Solid mass-flux potential Phi [kg/(m s)]: solid mass flux = -grad(Phi).
    Info<< "Creating field Phi\n" << endl;
    const fvBoundaryMesh& patches = mesh.boundary();

    wordList PhiBoundaryTypes(patches.size(), zeroGradientFvPatchScalarField::typeName);
    forAll(patches, patchi)
    {
        if (patches[patchi].name() == "inlet")
        {
            PhiBoundaryTypes[patchi] = fixedGradientFvPatchScalarField::typeName;
        }
        else if (patches[patchi].name() == "outlet")
        {
            PhiBoundaryTypes[patchi] = fixedValueFvPatchScalarField::typeName;
        }
        else if (patches[patchi].type() == "empty")
        {
            PhiBoundaryTypes[patchi] = "empty";
        }
        // wall, nozzle (and anything else): zeroGradient, the default above.
    }

    volScalarField Phi
    (
        IOobject
        (
            "Phi",
            runTime.timeName(),
            mesh,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh,
        dimensionedScalar(dimDensity*dimVelocity*dimLength, 0.0),
        PhiBoundaryTypes
    );

    const label inletPatchID = patches.findPatchID("inlet");
    const label outletPatchID = patches.findPatchID("outlet");

    if (inletPatchID == -1 || outletPatchID == -1)
    {
        FatalErrorInFunction
            << "Expected boundary patches \"inlet\" and \"outlet\" not "
            << "found on this mesh -- patch names: "
            << mesh.boundaryMesh().names()
            << exit(FatalError);
    }

    const scalar inletArea = gSum(patches[inletPatchID].magSf());
    const scalar inletGradient = inletMassFlux/inletArea;

    Info<< "  inlet area = " << inletArea << " m^2, prescribed gradient = "
        << inletGradient << " kg/(m^2 s) per unit m" << endl;

    refCast<fixedGradientFvPatchScalarField>
    (
        Phi.boundaryFieldRef()[inletPatchID]
    ).gradient() = inletGradient;

    Phi.boundaryFieldRef()[outletPatchID] == 0.0;

    // Solve for Phi.
    Info<< "Solving for Phi\n" << endl;
    fvScalarMatrix PhiEqn
    (
        fvm::laplacian(dimensionedScalar(dimless, 1.0), Phi) == S
    );
    PhiEqn.solve();

    // Recover Us = -grad(Phi) / (rhoSolid * (1 - porosityF)).
    Info<< "Recovering Us from grad(Phi)\n" << endl;
    volVectorField Us
    (
        IOobject
        (
            "Us",
            runTime.timeName(),
            mesh,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh,
        dimensionedVector(dimVelocity, Zero),
        wordList(patches.size(), zeroGradientFvPatchVectorField::typeName)
    );

    forAll(patches, patchi)
    {
        if (patches[patchi].type() == "empty")
        {
            Us.boundaryFieldRef().set
            (
                patchi,
                fvPatchField<vector>::New
                (
                    "empty",
                    mesh.boundary()[patchi],
                    Us
                )
            );
        }
    }

    // solidBulkDensity is exactly 0 wherever the case starts with no solid
    // charged yet (porosityF = 1, all Y<component> = 0) -- for LEI that is
    // exactly block 0 (z < 0.20, the freeboard/outlet AND the gasification
    // sink zone, per Decision 3: setFieldsDict's bed box only covers
    // z >= 0.20). Us is a *static* field (Decision 7 -- never re-solved at
    // run time), so it still has to be sensible there: once the descending
    // bed reaches block 0, whatever value is stored now is what it gets.
    // Treating "no solid yet" as Us = 0 would permanently strand solid
    // that later arrives -- worse than the uniform-Us jam this plan fixes.
    // Falling back to the bed's own volume-weighted mean bulk density
    // (computed below, from the cells that do carry solid) is a defensible
    // static-IC approximation for a composition that has not arrived yet;
    // it is not a physical measurement, so it is logged for visibility.
    const volScalarField solidBulkDensity(rhoSolid*(1.0 - porosityF));
    const scalar solidBulkDensityFloor = 1e-3; // kg/m^3, << any real bed value

    scalar sumV = 0.0, sumRhoV = 0.0;
    forAll(solidBulkDensity, celli)
    {
        if (solidBulkDensity[celli] > solidBulkDensityFloor)
        {
            sumV += mesh.V()[celli];
            sumRhoV += solidBulkDensity[celli]*mesh.V()[celli];
        }
    }
    reduce(sumV, sumOp<scalar>());
    reduce(sumRhoV, sumOp<scalar>());

    if (sumV < SMALL)
    {
        FatalErrorInFunction
            << "No cell has any solid charged -- cannot form a fallback "
            << "bulk density for the currently-empty cells." << exit(FatalError);
    }

    const scalar fallbackSolidBulkDensity = sumRhoV/sumV;
    Info<< "  fallback solid bulk density (currently-empty cells) = "
        << fallbackSolidBulkDensity << " kg/m^3" << endl;

    const volVectorField gradPhi(fvc::grad(Phi));
    vectorField& UsIn = Us.primitiveFieldRef();
    forAll(UsIn, celli)
    {
        const scalar rhoBulk =
            solidBulkDensity[celli] > solidBulkDensityFloor
          ? solidBulkDensity[celli]
          : fallbackSolidBulkDensity;

        UsIn[celli] = -gradPhi[celli]/rhoBulk;
    }
    Us.correctBoundaryConditions();

    const scalarField UsZ(Us.primitiveField().component(vector::Z));
    const scalarField UsX(Us.primitiveField().component(vector::X));

    Info<< "Us: min = " << gMin(UsZ) << " max = " << gMax(UsZ)
        << " m/s (z-component)\n"
        << "Us: max |x-component| = " << gMax(mag(UsX)) << " m/s" << endl;

    Us.write();

    Info<< "End\n" << endl;

    return 0;
}


// ************************************************************************* //
