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

Global
    rhoEqn

Description
    Solve the continuity for density.

\*---------------------------------------------------------------------------*/
// eqZx2uHGn042


// original code
/* Srho = pyrolysisZone.Srho();

{
    fvScalarMatrix rhoEqn
    (
        fvm::ddt(porosityF, rho)
      + fvc::div(phi)
      ==
        Srho * (1.0 - porosityF)
    );

    fvOptions.constrain(rhoEqn);

    rhoEqn.solve();

    fvOptions.correct(rho);
} */
 // till here original code


// DasteXar
 // for diagnostic only, can be removed later and use only the original code block
Srho = pyrolysisZone.Srho();

{
    /*
     * Sum the gas-species sources exactly as they are inserted
     * into the individual gas-species equations in YEqn.H.
     *
     * Units: kg/(m3_cell s)
     * diagnostic: 
     *          sum species RRg = source used by continuity
     */
    volScalarField speciesGasSourceDiagnostic
    (
        IOobject
        (
            "speciesGasSourceDiagnostic",
            mesh.time().timeName(),
            mesh,
            IOobject::NO_READ,
            IOobject::NO_WRITE,
            false
        ),
        mesh,
        dimensionedScalar
        (
            "zero",
            dimMass/dimVolume/dimTime,
            0.0
        )
    );

    forAll(Y, specieI)
    {
        speciesGasSourceDiagnostic +=
            pyrolysisZone.Srho(specieI)();
    }


    /*
     * This is the actual source inserted into rhoEqn, pEqn and pcEqn.
     *
     * It must be identical to speciesGasSourceDiagnostic for exact
     * gas-phase mass conservation.
     */
    volScalarField continuityGasSourceDiagnostic
    (
        IOobject
        (
            "continuityGasSourceDiagnostic",
            mesh.time().timeName(),
            mesh,
            IOobject::NO_READ,
            IOobject::NO_WRITE,
            false
        ),
        Srho*(scalar(1.0) - porosityF)
    );


    const dimensionedScalar speciesGasRateDiagnostic
    (
        fvc::domainIntegrate(speciesGasSourceDiagnostic)
    );

    const dimensionedScalar continuityGasRateDiagnostic
    (
        fvc::domainIntegrate(continuityGasSourceDiagnostic)
    );


    Info<< nl
        << "MASS DIAGNOSTIC:" << nl
        << "  sum species RRg [kg/s]       = "
        << speciesGasRateDiagnostic.value() << nl
        << "  source used by continuity    = "
        << continuityGasRateDiagnostic.value() << nl
        << "  source difference [kg/s]     = "
        << (
            continuityGasRateDiagnostic
          - speciesGasRateDiagnostic
        ).value()
        << endl;


    fvScalarMatrix rhoEqn
    (
        fvm::ddt(porosityF, rho)
      + fvc::div(phi)
      ==
        continuityGasSourceDiagnostic
    );

    fvOptions.constrain(rhoEqn);

    rhoEqn.solve();

    fvOptions.correct(rho);


    /*
     * Check the integrated residual of the discrete gas continuity
     * equation after solving rho.
     *
     * The final occurrence printed before "time step continuity errors"
     * is the relevant pressure-corrected value.
     */
    volScalarField continuityResidualDiagnostic
    (
        IOobject
        (
            "continuityResidualDiagnostic",
            mesh.time().timeName(),
            mesh,
            IOobject::NO_READ,
            IOobject::NO_WRITE,
            false
        ),
        fvc::ddt(porosityF, rho)
      + fvc::div(phi)
      - continuityGasSourceDiagnostic
    );

    const dimensionedScalar continuityResidualRateDiagnostic
    (
        fvc::domainIntegrate(continuityResidualDiagnostic)
    );

    Info<< "  continuity residual [kg/s]   = "
        << continuityResidualRateDiagnostic.value()
        << nl << endl;
}

// ************************************************************************* //
