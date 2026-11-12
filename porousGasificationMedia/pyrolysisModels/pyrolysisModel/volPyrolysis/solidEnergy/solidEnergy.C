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

\*---------------------------------------------------------------------------*/

#include "solidEnergy.H"
#include "fvm.H"
#include "fvc.H"
#include "fvMatrices.H"
#include "surfaceInterpolate.H"
#include "upwind.H"
#include "OStringStream.H"
#include "processorPolyPatch.H"
#include "cyclicPolyPatch.H"
#include "processorCyclicPolyPatch.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

defineTypeNameAndDebug(SolidEnergy, 0);

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

SolidEnergy::SolidEnergy
(
    const SolidFields& solid,
    const psiReactionThermo& gasThermo,
    const volScalarField& radiation,
    const volScalarField& chemistrySh,
    GasSolidHeatExchange& heatExchange,
    const SolidDiagnostics& solidDiagnostics,
    const Switch& equilibrium
)
:
    mesh_(solid.mesh),
    solidThermo_(solid.thermo),
    gasThermo_(gasThermo),
    chemistry_(solid.chemistry),
    T_(solid.T),
    rho_(solid.rho),
    K_(solid.thermo.K()),
    Ys_(solid.Ys),
    Ym_(solid.Ym),
    solidH_(solid.solidH),
    porosity_(solid.porosity),
    Us_(solid.Us),
    phiSolid_(solid.phiSolid),
    radiation_(radiation),
    chemistrySh_(chemistrySh),
    solidState_(solid.state),
    heatExchange_(heatExchange),
    solidDiagnostics_(solidDiagnostics),
    equilibrium_(equilibrium),
    active_(solid.active),
    radiationSh_
    (
        IOobject
        (
            "radiationSh",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedScalar("zero", dimEnergy/dimVolume/dimTime, 0.0)
    ),
    anisotropyK_
    (
        IOobject
        (
            "anisotropyK",
            mesh_.time().timeName(),
            mesh_,
            IOobject::READ_IF_PRESENT,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedTensor("one", dimless, tensor(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0))
    )
{}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

SolidEnergy::~SolidEnergy()
{}


// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //

Foam::tmp<Foam::volScalarField> SolidEnergy::solidHeatCapacity
(
    const volScalarField& totalYm
) const
{
    return solidState_.withSolidHeatCapacityFloor(totalYm*solidThermo_.Cp());
}

Foam::tmp<Foam::volScalarField> SolidEnergy::carrierHeatCapacity
(
    const volScalarField& totalYm
)
{
    const volScalarField preReactionYm
    (
        max
        (
            totalYm - mesh_.time().deltaT()*chemistry_.RRs()(),
            dimensionedScalar("zero", dimDensity, 0.0)
        )
    );

    volScalarField rhoCp(preReactionYm*solidThermo_.Cp());

    // Cp() is sum_i Ys_i*Cp_i, so with Ys_ holding the pre-reaction masses
    // instead of fractions it returns sum_i Ym_i*Cp_i. Ys_ is restored after.
    List<scalarField> postReactionYs(Ys_.size());
    forAll(Ys_, i)
    {
        postReactionYs[i] = Ys_[i].primitiveField();
        Ys_[i].primitiveFieldRef() = max
        (
            Ym_[i].primitiveField()
          - mesh_.time().deltaTValue()*chemistry_.RRs(i)().primitiveField(),
            scalar(0)
        );
    }

    const scalarField exactRhoCp(solidThermo_.Cp()().primitiveField());

    forAll(Ys_, i)
    {
        Ys_[i].primitiveFieldRef() = postReactionYs[i];
    }

    // preReactionYm*Cp() pairs the old mass with the new composition,
    // scaling Ts by Cp_pre/Cp_post each step. Below SMALL, Ys_ is
    // deriveYiFromYm()'s placeholder, so those cells keep it.
    forAll(rhoCp, cellI)
    {
        if (preReactionYm[cellI] > SMALL)
        {
            rhoCp[cellI] = exactRhoCp[cellI];
        }
    }

    return solidState_.withSolidHeatCapacityFloor(rhoCp);
}

void SolidEnergy::applyRegimeRules(const volScalarField& totalYm)
{
    T_ = solidState_.withDispersedRule(T_, gasThermo_.T());
    T_ = solidState_.withTraceRule(T_, totalYm, rho_, gasThermo_.T());
}

void SolidEnergy::storeH(const volScalarField& rhoCp)
{
    solidH_.ref() = rhoCp*T_;
    solidH_.correctBoundaryConditions();
}

Foam::tmp<Foam::fvScalarMatrix> SolidEnergy::conductionWithinSolid
(
    const volTensorField& composedK
)
{
    const surfaceScalarField solidPresenceAtFaces
    (
        fvc::interpolate(solidState_.solidPresent())
    );

    // Simplistic immersed boundary for heat transport in solid phase.
    tmp<fvScalarMatrix> tTLap(new fvScalarMatrix(fvm::laplacian(composedK, T_)));
    fvScalarMatrix& TLap = tTLap.ref();

    // Setting face fluxes on the border of porous media to 0.
    // this should be instead flux due to the macroscopic motion to smooth at the edges
    forAll(solidPresenceAtFaces,faceI)
    {
       if ( (solidPresenceAtFaces[faceI] > 0) and (solidPresenceAtFaces[faceI] < 1) )
       {
           TLap.upper()[faceI] = 0.;
       }
    }
    forAll(solidPresenceAtFaces.boundaryField(),patchI)
    {
       if (isA<processorPolyPatch>(mesh_.boundaryMesh()[patchI]) or isA<cyclicPolyPatch>(mesh_.boundaryMesh()[patchI]) or isA<processorCyclicPolyPatch>(mesh_.boundaryMesh()[patchI]) )
       {
           forAll(solidPresenceAtFaces.boundaryField()[patchI],faceI)
           {
               if ( (solidPresenceAtFaces.boundaryField()[patchI][faceI] > 0) and (solidPresenceAtFaces.boundaryField()[patchI][faceI] < 1) )
               {
                   TLap.boundaryCoeffs()[patchI][faceI] = 0;
                   TLap.internalCoeffs()[patchI][faceI] = 0;
               }
           }
       }
    }

    TLap.diag() = 0;
    TLap.negSumDiag();
    // Exact on orthogonal meshes. On non-orthogonal ones, zeroing the source
    // drops the correction from div(composedK): spurious at the solid edge,
    // negligible inside the bed in relevant cases.
    TLap.source() = 0.;

    return tTLap;
}

Foam::tmp<Foam::volScalarField> SolidEnergy::solidSources()
{
    const volScalarField& connectedAtStepStart = solidState_.connectedAtStepStart();

    // Stored by volPyrolysis before the species step, so the gas EEqn
    // adds back the same joules. The exchange gates itself; gating
    // again here would be a no-op, not a safeguard.
    const volScalarField& interphaseHeatTransfer =
        heatExchange_.heatTransfer();
    // An agent's mass source is gated like every Srho_i: zero outside the
    // reacting (connectedAtStepStart) cells, so no gate is added here.
    const volScalarField& agentHeatUp = heatExchange_.agentHeatUp();

    // radiationSh_ carries no connected gate of its own: as a cell
    // empties, rhoCp collapses toward its SMALL floor while the T^4
    // radiative sink stays full strength.
    volScalarField solidRadiationSource = connectedAtStepStart*radiationSh_;

    // Same collapsing-rhoCp gap as solidRadiationSource, for the
    // reaction heat: chemistrySh_ is set from RRs_/RRg_ (nonzero
    // wherever there is solid) with no connected gate of its own.
    volScalarField solidChemistrySource = connectedAtStepStart*chemistrySh_;

    return
        solidChemistrySource // eqZx2uHGn004, eqZx2uHGn017
      - interphaseHeatTransfer // eqZx2uHGn005
      - agentHeatUp
      + solidRadiationSource;
}

Foam::tmp<Foam::volScalarField> SolidEnergy::advectiveEnthalpyFlux
(
    const PtrList<surfaceScalarField>& phiYm
) const
{
    tmp<surfaceScalarField> tFlux;

    // Each specie carries Cp_i*Ts on its own mass flux, the one that moved
    // Ym_i: a uniform Ts then stays uniform for any div(phiSolid) scheme.
    // Upwind, so a receiver ends up at a convex combination of donors.
    forAll(phiYm, i)
    {
        const volScalarField hsI(solidThermo_.specieCp(i)*T_);

        tmp<surfaceScalarField> tFluxI
        (
            phiYm[i]*upwind<scalar>(mesh_, phiYm[i]).interpolate(hsI)
        );

        if (tFlux.valid())
        {
            tFlux.ref() += tFluxI;
        }
        else
        {
            tFlux = tFluxI;
        }
    }

    return fvc::surfaceIntegrate(tFlux);
}

void SolidEnergy::solveEnthalpyBalance
(
    const volTensorField& composedK,
    const volScalarField& rhoCpCarrier,
    const volScalarField& HOld,
    const PtrList<surfaceScalarField>& phiYm
)
{
    const dimensionedScalar rDeltaT(1.0/mesh_.time().deltaT());

    const tmp<volScalarField> tSources(solidSources());

    const tmp<fvScalarMatrix> tTLap(conductionWithinSolid(composedK));

    const volScalarField divEnthalpyFlux(advectiveEnthalpyFlux(phiYm));

    // With Euler the first two terms are (rhoCpCarrier*Ts - HOld)/dt: dH/dt
    // minus sum_i RRs_i*Cp_i*Ts, taken implicitly (#134). fvm::ddt keeps the
    // case's ddtScheme; the second is zero unless transport changed the mass.
    fvScalarMatrix TEqn
    (
        fvm::ddt(rhoCpCarrier, T_)
      + rDeltaT*(rhoCpCarrier*T_.oldTime() - HOld)
      + divEnthalpyFlux
      - tTLap()
    ==
        tSources()
    );

    // Start from the transported state: a solve that stops at once, its
    // residual already under tolerance, must not drop the explicit transport.
    T_.primitiveFieldRef() =
    (
        (HOld - mesh_.time().deltaT()*divEnthalpyFlux)/rhoCpCarrier
    )().primitiveField();

    TEqn.relax();
    TEqn.solve();
}

void SolidEnergy::abortOnInvalidTs
(
    const word& stage,
    const label badCell,
    const volScalarField& HOld,
    const volScalarField& rhoCpCarrier,
    const volScalarField& totalYm
) const
{
    if (badCell == -1)
    {
        return;
    }

    const scalar gate = solidState_.connectedAtStepStart()[badCell];

    OStringStream context;
    context
        << "    solidH old   = " << HOld[badCell] << nl
        << "    rhoCp carrier= " << rhoCpCarrier[badCell] << nl
        << "    sum(Ym)      = " << totalYm[badCell] << nl
        << "    chemistrySh  = " << gate*chemistrySh_[badCell] << nl
        << "    heatTransfer = "
        << heatExchange_.heatTransfer()[badCell] << nl
        << "    agentHeatUp  = "
        << heatExchange_.agentHeatUp()[badCell] << nl
        << "    radiationSh  = " << gate*radiationSh_[badCell] << nl
        << "    Us           = " << Us_[badCell] << nl
        << "    porosity     = " << porosity_[badCell] << nl
        << "    solidPresent = " << solidState_.solidPresent()[badCell];

    solidDiagnostics_.abort(stage, T_, badCell, context.str());
}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

void SolidEnergy::solve(const PtrList<surfaceScalarField>& phiYm)
{
// eqZx2uHGn047

    if (debug)
    {
        Info<< "SolidEnergy::solve()" << endl;
    }

    if (!active_)
    {
        return;
    }

    radiationSh_ = radiation_;

    if (equilibrium_)
    {
        return;
    }

    const volTensorField composedK(K_ * solidState_.solidFraction() * anisotropyK_);

    // Not solidH_.oldTime(): bed collapse and regime flips write solidH_
    // after the solve, and this is the value they left.
    const volScalarField HOld("solidHOld", solidH_);

    forAll(Ym_, i)
    {
        Ym_[i].correctBoundaryConditions();
    }

    // The mass the species step left, reaction and transport included.
    const volScalarField totalYm(SolidState::totalMass(Ym_));

    // No solidPresent factor on either heat capacity: solidH_ carries none,
    // and masking one and not the other is a temperature of any size.
    const volScalarField rhoCpCarrier
    (
        "rhoCpCarrier",
        carrierHeatCapacity(totalYm)
    );

    T_.correctBoundaryConditions();
    solidState_.correctSolidPresentBoundaryConditions();

    solveEnthalpyBalance(composedK, rhoCpCarrier, HOld, phiYm);

    abortOnInvalidTs
    (
        "the solid enthalpy balance in SolidEnergy::solve()",
        solidDiagnostics_.firstInvalidTemperature
        (
            T_,
            solidState_.massBeyondTrace(totalYm, rho_)().primitiveField()
        ),
        HOld,
        rhoCpCarrier,
        totalYm
    );

    // Only bites with the check above switched off: no negative enthalpy.
    T_.max(dimensionedScalar("zero", dimTemperature, 0.0));

    solidThermo_.correct();
    applyRegimeRules(totalYm);

    abortOnInvalidTs
    (
        "the regime rules on Ts in SolidEnergy::solve()",
        solidDiagnostics_.firstInvalidTemperature(T_),
        HOld,
        rhoCpCarrier,
        totalYm
    );

    // The one place solidH_ is paired with a heat capacity: the mass that
    // remains, so the next step's old-time term is this Ts.
    storeH(solidHeatCapacity(totalYm)());
    T_.correctBoundaryConditions();

    solidDiagnostics_.reportTemperatureRange(T_);
}



// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// ************************************************************************* //
