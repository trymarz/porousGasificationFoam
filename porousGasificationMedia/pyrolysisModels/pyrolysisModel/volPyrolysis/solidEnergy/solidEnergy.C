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
    const fvMesh& mesh,
    HGSSolidThermo& solidThermo,
    const psiReactionThermo& gasThermo,
    const porousThermoSolidChemistryModel<HGSSolidThermo>& chemistry,
    PtrList<volScalarField>& Ym,
    autoPtr<volScalarField>& solidH,
    const volScalarField& porosity,
    const volVectorField& Us,
    const surfaceScalarField& phiSolid,
    const volScalarField& radiation,
    const volScalarField& chemistrySh,
    SolidState& solidState,
    GasSolidHeatExchange& heatExchange,
    const SolidStateChecker& solidStateChecker,
    const Switch& equilibrium,
    const Switch& active
)
:
    mesh_(mesh),
    solidThermo_(solidThermo),
    gasThermo_(gasThermo),
    chemistry_(chemistry),
    T_(solidThermo.T()),
    rho_(solidThermo.rho()),
    K_(solidThermo.K()),
    Ys_(solidThermo.composition().Y()),
    Ym_(Ym),
    solidH_(solidH),
    porosity_(porosity),
    Us_(Us),
    phiSolid_(phiSolid),
    radiation_(radiation),
    chemistrySh_(chemistrySh),
    solidState_(solidState),
    heatExchange_(heatExchange),
    solidStateChecker_(solidStateChecker),
    equilibrium_(equilibrium),
    active_(active),
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

Foam::tmp<Foam::volScalarField> SolidEnergy::preReactionMass
(
    const volScalarField& totalYm
) const
{
    return max
    (
        totalYm - mesh_.time().deltaT()*chemistry_.RRs()(),
        dimensionedScalar("zero", dimDensity, 0.0)
    );
}

Foam::tmp<Foam::volScalarField> SolidEnergy::preReactionHeatCapacity
(
    const volScalarField& preReactionYm
)
{
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

void SolidEnergy::recoverTsFromH
(
    const volScalarField& rhoCp,
    const volScalarField& totalYm,
    const bool dispersedRule
)
{
    if (dispersedRule)
    {
        T_ = solidState_.withDispersedRule(solidH_()/rhoCp, gasThermo_.T());
        T_ = solidState_.withTraceRule(T_, totalYm, rho_, gasThermo_.T());
    }
    else
    {
        T_ = solidState_.withTraceRule
        (
            solidH_()/rhoCp,
            totalYm,
            rho_,
            gasThermo_.T()
        );
    }
}

void SolidEnergy::storeH(const volScalarField& rhoCp)
{
    solidH_().ref() = rhoCp*T_;
    solidH_().correctBoundaryConditions();
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

void SolidEnergy::solveTsEquation
(
    const volTensorField& composedK,
    const volScalarField& rhoCp,
    const volScalarField& solidBeyondTrace
)
{
    const volScalarField& connectedAtStepStart = solidState_.connectedAtStepStart();

    // Evaluated once and stored, so the gas EEqn adds back the same
    // joules after TEqn has moved T_. The exchange gates itself;
    // gating again here would be a no-op, not a safeguard.
    const volScalarField& interphaseHeatTransfer =
        heatExchange_.updateHeatTransfer();
    const volScalarField& heatUpGas = heatExchange_.heatUpGas();

    // radiationSh_ carries no connected gate of its own: as a cell
    // empties, rhoCp collapses toward its SMALL floor while the T^4
    // radiative sink stays full strength.
    volScalarField solidRadiationSource = connectedAtStepStart*radiationSh_;

    // Same collapsing-rhoCp gap as solidRadiationSource, for the
    // reaction heat: chemistrySh_ is set from RRs_/RRg_ (nonzero
    // wherever there is solid) with no connected gate of its own.
    volScalarField solidChemistrySource = connectedAtStepStart*chemistrySh_;

    const tmp<fvScalarMatrix> tTLap(conductionWithinSolid(composedK));

    fvScalarMatrix TEqn
    (
        fvm::ddt(rhoCp,T_)
      - tTLap()
    ==
        solidChemistrySource // eqZx2uHGn004, eqZx2uHGn017
      - interphaseHeatTransfer // eqZx2uHGn005
      - heatUpGas
      + solidRadiationSource
    );

    TEqn.relax();
    TEqn.solve();

    const label badTEqnCell =
        solidStateChecker_.firstInvalidTemperature
        (
            T_,
            solidBeyondTrace.primitiveField()
        );

    if (badTEqnCell != -1)
    {
        OStringStream context;
        context
            << "    rhoCp        = " << rhoCp[badTEqnCell] << nl
            << "    chemistrySh  = "
            << solidChemistrySource[badTEqnCell] << nl
            << "    heatTransfer = "
            << interphaseHeatTransfer[badTEqnCell] << nl
            << "    heatUpGas    = " << heatUpGas[badTEqnCell] << nl
            << "    radiationSh  = "
            << solidRadiationSource[badTEqnCell] << nl
            << "    porosity     = " << porosity_[badTEqnCell];

        solidStateChecker_.abort
        (
            "the solid energy equation in solveTsEquation()",
            T_,
            badTEqnCell,
            context.str()
        );
    }
}

void SolidEnergy::advectSolidH
(
    const volScalarField& rhoCp,
    const volScalarField& totalYm
)
{
    const surfaceScalarField& solidFlux = phiSolid_;

    // Enthalpy rides the solid MASS flux, not phiSolid: div(phiSolid)
    // is nonlinear, so it would give solidH and Ym different face
    // values. phiYm here matches SolidSpeciesTransport::solve()'s own flux.
    surfaceScalarField phiYm
    (
        fvc::flux(solidFlux, Ym_[0], "div(phiSolid)")
    );
    for (label i = 1; i < Ym_.size(); ++i)
    {
        phiYm += fvc::flux(solidFlux, Ym_[i], "div(phiSolid)");
    }

    // hs upwinds on the mass flux: a receiver ends up holding the
    // convex combination (solidH + m_in*hs_donor)/(totalYm + m_in).
    // Cp*T_, not solidH/totalYm, to match rhoCp's own floor.
    volScalarField hs(solidThermo_.Cp()*T_);

    dimensionedScalar ovDt = pow(mesh_.time().deltaT(),-1);
    fvScalarMatrix sHEqn
    (
        fvm::Sp(ovDt,solidH_()) - ovDt*solidH_()
      + fvc::surfaceIntegrate
        (
            phiYm*upwind<scalar>(mesh_, phiYm).interpolate(hs)
        )
    );

    sHEqn.relax();
    sHEqn.solve();

    const volScalarField& sH = solidH_();

    const label badSolidHCell =
        solidStateChecker_.firstInvalidExtensive(sH);

    if (badSolidHCell != -1)
    {
        OStringStream context;
        context
            << "    Us           = " << Us_[badSolidHCell] << nl
            << "    Ts           = " << T_[badSolidHCell] << nl
            << "    rhoCp        = " << rhoCp[badSolidHCell] << nl
            << "    sum(Ym)      = " << totalYm[badSolidHCell] << nl
            << "    porosity     = " << porosity_[badSolidHCell];

        solidStateChecker_.abort
        (
            "the solid enthalpy advection in advectSolidH()",
            sH,
            badSolidHCell,
            context.str()
        );
    }

    solidH_().max(0);
    solidH_().correctBoundaryConditions();
}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

void SolidEnergy::solve()
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

    forAll(Ym_, i)
    {
        Ym_[i].correctBoundaryConditions();
    }

    const volScalarField totalYm(SolidState::totalMass(Ym_));

    // What the two Ts guards below are judged on.
    const volScalarField solidBeyondTrace
    (
        solidState_.massBeyondTrace(totalYm, rho_)
    );

    // The heat capacity of the solid the cell actually holds. No
    // solidPresent factor: solidH_ carries none either, and
    // masking one and not the other is a temperature of any size.
    const volScalarField rhoCp(solidHeatCapacity(totalYm));

    // Only the trace rule: the dispersed rule was applied at the end
    // of the last step, and solidH_ was restated from that Ts.
    recoverTsFromH(rhoCp, totalYm, false);

    const label badTsCell =
        solidStateChecker_.firstInvalidTemperature
        (
            T_,
            solidBeyondTrace.primitiveField()
        );

    if (badTsCell != -1)
    {
        OStringStream context;
        context
            << "    solidH       = "
            << solidH_()[badTsCell] << nl
            << "    sum(Ym)      = " << totalYm[badTsCell] << nl
            << "    rhoCp        = " << rhoCp[badTsCell] << nl
            << "    porosity     = " << porosity_[badTsCell] << nl
            << "    solidPresent = " << solidState_.solidPresent()[badTsCell];

        solidStateChecker_.abort
        (
            "Ts = solidH/rhoCp at the head of SolidEnergy::solve()",
            T_,
            badTsCell,
            context.str()
        );
    }

    T_.correctBoundaryConditions();
    solidState_.correctSolidPresentBoundaryConditions();

    solveTsEquation(composedK, rhoCp, solidBeyondTrace);
    storeH(rhoCp);
    advectSolidH(rhoCp, totalYm);
}

void SolidEnergy::recoverTemperatureAfterReaction()
{
// eqZx2uHGn047

    if (debug)
    {
        Info<< "SolidEnergy::recoverTemperatureAfterReaction()" << endl;
    }

    if (!active_ || equilibrium_)
    {
        return;
    }

    const volScalarField totalYm(SolidState::totalMass(Ym_));
    solidThermo_.correct();

    const volScalarField rhoCp(solidHeatCapacity(totalYm));

    // The mass SolidSpeciesTransport::solve() has just converted to gas. Ym_
    // carries the loss, solidH_ does not: it was built in
    // solve() on what the cell held before the reaction.
    const volScalarField preReactionYm(preReactionMass(totalYm));

    // Divided by the mass that carried the enthalpy: gas-bound solid takes
    // its sensible enthalpy with it, so losing mass must not raise Ts. No
    // solidPresent factor: it lags a stage, and would start the cell at 0 K.
    const volScalarField rhoCpPreReaction(preReactionHeatCapacity(preReactionYm));
    recoverTsFromH(rhoCpPreReaction, totalYm, true);

    const label badCell =
        solidStateChecker_.firstInvalidTemperature(T_);

    if (badCell != -1)
    {
        OStringStream context;
        context
            << "    solidH       = "
            << solidH_()[badCell] << nl
            << "    sum(Ym)      = " << totalYm[badCell] << nl
            << "    sum(Ym) pre  = " << preReactionYm[badCell] << nl
            << "    rhoCp        = " << rhoCp[badCell] << nl
            << "    porosity     = " << porosity_[badCell] << nl
            << "    solidPresent = " << solidState_.solidPresent()[badCell];

        solidStateChecker_.abort
        (
            "the Ts recovery in recoverTemperatureAfterReaction()",
            T_,
            badCell,
            context.str()
        );
    }

    // Restated on the mass that remains, so solve() reads
    // back this Ts next step instead of re-inflating it.
    storeH(rhoCp);
    T_.correctBoundaryConditions();

    solidStateChecker_.reportTemperatureRange(T_);
}



// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// ************************************************************************* //
