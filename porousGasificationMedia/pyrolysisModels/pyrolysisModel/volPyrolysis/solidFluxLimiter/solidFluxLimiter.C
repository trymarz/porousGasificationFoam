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

#include "solidFluxLimiter.H"

#include "fvcFlux.H"
#include "surfaceInterpolate.H"
#include "upwind.H"
#include "slicedSurfaceFields.H"
#include "syncTools.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //

tmp<surfaceScalarField> SolidFluxLimiter::solidVolFlux() const
{
    tmp<surfaceScalarField> tSolidVolFlux
    (
        new surfaceScalarField
        (
            IOobject
            (
                "solidVolFlux",
                time_.timeName(),
                mesh_,
                IOobject::NO_READ,
                IOobject::NO_WRITE
            ),
            mesh_,
            dimensionedScalar("zero", dimVolume/dimTime, 0.0)
        )
    );

    if (advectSolidFields_)
    {
        tSolidVolFlux.ref() = mesh_.Sf() & fvc::interpolate(Us_, "Us");
    }

    return tSolidVolFlux;
}


void SolidFluxLimiter::accumulateFaceFlux
(
    const surfaceScalarField& phi,
    const surfaceScalarField& fluxScale,
    scalarField& sumOut,
    scalarField& sumIn
) const
{
    sumOut = 0.0;
    sumIn = 0.0;

    const labelUList& owner = mesh_.owner();
    const labelUList& neighbour = mesh_.neighbour();

    forAll(phi, faceI)
    {
        const scalar faceFlux = fluxScale[faceI]*phi[faceI];

        if (faceFlux > 0.0)
        {
            sumOut[owner[faceI]] += faceFlux;
            sumIn[neighbour[faceI]] += faceFlux;
        }
        else
        {
            sumIn[owner[faceI]] -= faceFlux;
            sumOut[neighbour[faceI]] -= faceFlux;
        }
    }

    forAll(phi.boundaryField(), patchI)
    {
        const fvsPatchScalarField& phiP = phi.boundaryField()[patchI];
        const fvsPatchScalarField& fluxScaleP = fluxScale.boundaryField()[patchI];
        const labelUList& faceCells = mesh_.boundary()[patchI].faceCells();

        forAll(phiP, i)
        {
            const scalar faceFlux = fluxScaleP[i]*phiP[i];

            if (faceFlux > 0.0)
            {
                sumOut[faceCells[i]] += faceFlux;
            }
            else
            {
                sumIn[faceCells[i]] -= faceFlux;
            }
        }
    }
}


void SolidFluxLimiter::solidFluxBudgets
(
    const surfaceScalarField& phiSolid,
    PtrList<surfaceScalarField>& phiYm,
    PtrList<volScalarField>& RRsolid,
    tmp<surfaceScalarField>& tPhiYmTotal,
    tmp<surfaceScalarField>& tPhiSolidVol,
    tmp<volScalarField>& tAlphaS,
    tmp<volScalarField>& tRRpor
)
{
    const dimensionedScalar rhoSolidFloor
    (
        "rhoSolidFloor",
        dimDensity,
        SMALL
    );

    phiYm.setSize(Ym_.size());
    RRsolid.setSize(Ym_.size());

    forAll(Ym_, i)
    {
        Ym_[i].correctBoundaryConditions();

        phiYm.set(i, fvc::flux(phiSolid, Ym_[i], "div(phiSolid)").ptr());
        RRsolid.set(i, chemistry_.RRs(i).ptr());
    }

    volScalarField totalYm(Ym_[0]);

    tPhiYmTotal = tmp<surfaceScalarField>(new surfaceScalarField(phiYm[0]));
    surfaceScalarField& phiYmTotal = tPhiYmTotal.ref();

    for (label i = 1; i < Ym_.size(); ++i)
    {
        totalYm += Ym_[i];
        phiYmTotal += phiYm[i];
    }

    tAlphaS = totalYm/max(rho_, rhoSolidFloor);

    // RRpor = -sum_i RRs_i/rho_i = d(porosity)/dt, so -RRpor is
    // d(alphaS)/dt: chemistry densifying a cell leaves it that much
    // less room for incoming flux.
    tRRpor = chemistry_.RRpor(T_);

    // Arriving mass takes the specific volume of where it came from,
    // so it's interpolated upwind of the flux (exact if species share
    // a density, second-order otherwise).
    tPhiSolidVol =
        phiYmTotal
       *upwind<scalar>(mesh_, phiSolid).interpolate
        (
            1.0/max(rho_, rhoSolidFloor)
        );
}


void SolidFluxLimiter::solidDonorLimit
(
    const PtrList<surfaceScalarField>& phiYm,
    const PtrList<volScalarField>& RRsolid,
    const surfaceScalarField& fluxScale,
    const bool credit,
    scalarField& donorScale
) const
{
    const scalarField& V = mesh_.V();
    const scalar rDeltaT = 1.0/time_.deltaTValue();

    scalarField sumOut(mesh_.nCells(), Zero);
    scalarField sumIn(mesh_.nCells(), Zero);

    donorScale = 1.0;

    forAll(Ym_, i)
    {
        accumulateFaceFlux(phiYm[i], fluxScale, sumOut, sumIn);

        forAll(donorScale, cellI)
        {
            // Mass of specie i this cell can give up: what it holds,
            // minus what chemistry removes.
            const scalar canLeave = max
            (
                0.0,
                (Ym_[i][cellI]*rDeltaT + RRsolid[i][cellI])*V[cellI]
              + (credit ? sumIn[cellI] : 0.0)
            );

            // Skip cells with no outflow: a zero factor here would
            // wrongly "limit" faces carrying no solid at all.
            if (sumOut[cellI] > SMALL)
            {
                donorScale[cellI] = min
                (
                    donorScale[cellI],
                    min(1.0, canLeave/sumOut[cellI])
                );
            }
        }
    }
}


void SolidFluxLimiter::solidReceiverLimit
(
    const surfaceScalarField& phiSolidVol,
    const volScalarField& alphaS,
    const volScalarField& RRpor,
    const surfaceScalarField& fluxScale,
    const bool credit,
    scalarField& receiverScale
) const
{
    const scalarField& V = mesh_.V();
    const scalar rDeltaT = 1.0/time_.deltaTValue();
    const scalar alphaSMax = 1.0 - minPorosity_;

    scalarField sumOut(mesh_.nCells(), Zero);
    scalarField sumIn(mesh_.nCells(), Zero);

    accumulateFaceFlux(phiSolidVol, fluxScale, sumOut, sumIn);

    forAll(receiverScale, cellI)
    {
        // Solid volume this cell still has room for, after chemistry
        // has taken its own share of it.
        const scalar room = max
        (
            0.0,
            ((alphaSMax - alphaS[cellI])*rDeltaT + RRpor[cellI])*V[cellI]
          + (credit ? sumOut[cellI] : 0.0)
        );

        receiverScale[cellI] =
            sumIn[cellI] > SMALL
          ? min(1.0, room/sumIn[cellI])
          : 1.0;
    }
}


void SolidFluxLimiter::applySolidFaceLimit
(
    const surfaceScalarField& phiYmTotal,
    const scalarField& donorScale,
    const scalarField& receiverScale,
    scalarField& allFluxScale,
    surfaceScalarField& fluxScale
) const
{
    const labelUList& owner = mesh_.owner();
    const labelUList& neighbour = mesh_.neighbour();

    scalarField& fluxScaleIn = fluxScale;
    surfaceScalarField::Boundary& fluxScaleBf = fluxScale.boundaryFieldRef();

    forAll(fluxScaleIn, faceI)
    {
        const label own = owner[faceI];
        const label nei = neighbour[faceI];

        if (phiYmTotal[faceI] > 0.0)
        {
            fluxScaleIn[faceI] = min
            (
                fluxScaleIn[faceI],
                min(donorScale[own], receiverScale[nei])
            );
        }
        else
        {
            fluxScaleIn[faceI] = min
            (
                fluxScaleIn[faceI],
                min(donorScale[nei], receiverScale[own])
            );
        }
    }

    forAll(fluxScaleBf, patchI)
    {
        fvsPatchScalarField& fluxScaleP = fluxScaleBf[patchI];
        const fvsPatchScalarField& phiP =
            phiYmTotal.boundaryField()[patchI];
        const labelUList& faceCells =
            mesh_.boundary()[patchI].faceCells();

        forAll(fluxScaleP, i)
        {
            // Only this side is visible here; a coupled patch gets the
            // other side's factor from the sync below. On a real
            // boundary: inflow held by room, outflow by mass.
            fluxScaleP[i] = min
            (
                fluxScaleP[i],
                phiP[i] > 0.0
              ? donorScale[faceCells[i]]
              : receiverScale[faceCells[i]]
            );
        }
    }

    // fluxScale slices allFluxScale (patch faces included), so both sides
    // of a coupled face meet here and keep the tighter factor.
    syncTools::syncFaceList(mesh_, allFluxScale, minEqOp<scalar>());
}


void SolidFluxLimiter::reportSolidFluxLimiter
(
    const PtrList<surfaceScalarField>& phiYm,
    const PtrList<volScalarField>& RRsolid,
    const surfaceScalarField& phiSolidVol,
    const volScalarField& alphaS,
    const volScalarField& RRpor,
    const surfaceScalarField& fluxScale
) const
{
    const scalarField& V = mesh_.V();
    const scalar deltaT = time_.deltaTValue();
    const scalar alphaSMax = 1.0 - minPorosity_;

    const scalarField& fluxScaleIn = fluxScale;
    const surfaceScalarField::Boundary& fluxScaleBf = fluxScale.boundaryField();

    scalar nLimited = 0.0;
    scalar withheld = 0.0;
    scalar minFluxScale = 1.0;

    forAll(fluxScaleIn, faceI)
    {
        minFluxScale = min(minFluxScale, fluxScaleIn[faceI]);

        if (fluxScaleIn[faceI] < 1.0 - SMALL)
        {
            nLimited += 1.0;
            withheld +=
                (1.0 - fluxScaleIn[faceI])*mag(phiSolidVol[faceI])*deltaT;
        }
    }

    forAll(fluxScaleBf, patchI)
    {
        // A coupled face is counted from both sides, so weight by
        // half to total per physical face.
        const scalar weight =
            mesh_.boundary()[patchI].coupled() ? 0.5 : 1.0;

        const fvsPatchScalarField& fluxScaleP = fluxScaleBf[patchI];
        const fvsPatchScalarField& phiVolP =
            phiSolidVol.boundaryField()[patchI];

        forAll(fluxScaleP, i)
        {
            minFluxScale = min(minFluxScale, fluxScaleP[i]);

            if (fluxScaleP[i] < 1.0 - SMALL)
            {
                nLimited += weight;
                withheld +=
                    weight*(1.0 - fluxScaleP[i])*mag(phiVolP[i])*deltaT;
            }
        }
    }

    // Measured from the state the limited flux will actually produce,
    // not from the inequalities it was built from - sweeps are capped,
    // so convergence isn't guaranteed.
    scalar maxUndershoot = 0.0;
    scalar YmScale = SMALL;

    scalarField sumOut(mesh_.nCells(), Zero);
    scalarField sumIn(mesh_.nCells(), Zero);

    forAll(Ym_, i)
    {
        YmScale = max(YmScale, gMax(Ym_[i]));

        accumulateFaceFlux(phiYm[i], fluxScale, sumOut, sumIn);

        forAll(Ym_[i], cellI)
        {
            const scalar YmNew =
                Ym_[i][cellI]
              + deltaT
               *(
                    RRsolid[i][cellI]
                  + (sumIn[cellI] - sumOut[cellI])/V[cellI]
                );

            maxUndershoot = max(maxUndershoot, -YmNew);
        }
    }

    accumulateFaceFlux(phiSolidVol, fluxScale, sumOut, sumIn);

    scalar maxOvershoot = 0.0;

    forAll(alphaS, cellI)
    {
        const scalar alphaSNew =
            alphaS[cellI]
          + deltaT
           *(
              - RRpor[cellI]
              + (sumIn[cellI] - sumOut[cellI])/V[cellI]
            );

        maxOvershoot = max(maxOvershoot, alphaSNew - alphaSMax);
    }

    nLimited = returnReduce(nLimited, sumOp<scalar>());
    withheld = returnReduce(withheld, sumOp<scalar>());
    minFluxScale = returnReduce(minFluxScale, minOp<scalar>());
    maxOvershoot = returnReduce(maxOvershoot, maxOp<scalar>());
    maxUndershoot = returnReduce(maxUndershoot, maxOp<scalar>());

    if (nLimited > 0.5)
    {
        Info<< "solid flux limiter: faces limited = "
            << label(nLimited + 0.5)
            << ", min scale factor = " << minFluxScale
            << ", solid volume withheld = " << withheld << " m3";

        if
        (
            maxOvershoot > solidStateTolerance_
         || maxUndershoot > solidStateTolerance_*YmScale
        )
        {
            Info<< ", NOT converged in " << nSolidFluxLimiterCorrectors_
                << " sweeps: residual packing = " << maxOvershoot
                << ", residual mass deficit = " << maxUndershoot;
        }

        Info<< endl;
    }
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

SolidFluxLimiter::SolidFluxLimiter
(
    const dictionary& coeffs,
    const fvMesh& mesh,
    const volVectorField& Us,
    PtrList<volScalarField>& Ym,
    const volScalarField& rho,
    const volScalarField& T,
    const porousThermoSolidChemistryModel<HGSSolidThermo>& chemistry,
    const Switch& active
)
:
    mesh_(mesh),
    time_(mesh.time()),
    Us_(Us),
    Ym_(Ym),
    rho_(rho),
    T_(T),
    chemistry_(chemistry),
    active_(active),
    advectSolidFields_(coeffs.lookupOrDefault("advectSolidFields", true)),
    nSolidFluxLimiterCorrectors_
    (
        coeffs.lookupOrDefault<label>("nSolidFluxLimiterCorrectors", 3)
    ),
    minPorosity_(coeffs.lookupOrDefault<scalar>("minPorosity", 1e-4)),
    solidStateTolerance_
    (
        coeffs.lookupOrDefault<scalar>("solidStateTolerance", 1e-8)
    )
{
    if (minPorosity_ <= 0.0)
    {
        FatalIOErrorInFunction(coeffs)
            << "minPorosity must be positive, but is " << minPorosity_
            << "." << nl
            << "Every gas equation multiplies porosityF straight into its"
            << " ddt coefficient, so a cell packed to zero gas volume leaves"
            << " that matrix with a zero diagonal and the solve divides by"
            << " zero. Pick the smallest gas fraction the case is meant to"
            << " reach, not zero."
            << exit(FatalIOError);
    }

    Info << "advectSolidFields        " << advectSolidFields_  << endl;
    Info << "nSolidFluxLimiterCorrectors " << nSolidFluxLimiterCorrectors_
         << endl;
    Info << "minPorosity              " << minPorosity_ << endl;
}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

SolidFluxLimiter::~SolidFluxLimiter()
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

tmp<surfaceScalarField> SolidFluxLimiter::limit()
{
    tmp<surfaceScalarField> tPhiSolid = solidVolFlux();

    if (!active_ || !advectSolidFields_ || nSolidFluxLimiterCorrectors_ < 1)
    {
        return tPhiSolid;
    }

    surfaceScalarField& phiSolid = tPhiSolid.ref();

    // Budgets follow the update Ym_i^new = Ym_i + dt*(RRs_i -
    // div(phiYm_i)), so scaling phiSolid scales every phiYm by the
    // same factor (the scheme only reads the upwind direction).
    PtrList<surfaceScalarField> phiYm;
    PtrList<volScalarField> RRsolid;
    tmp<surfaceScalarField> tPhiYmTotal;
    tmp<surfaceScalarField> tPhiSolidVol;
    tmp<volScalarField> tAlphaS;
    tmp<volScalarField> tRRpor;

    solidFluxBudgets
    (
        phiSolid,
        phiYm,
        RRsolid,
        tPhiYmTotal,
        tPhiSolidVol,
        tAlphaS,
        tRRpor
    );

    const surfaceScalarField& phiYmTotal = tPhiYmTotal();
    const surfaceScalarField& phiSolidVol = tPhiSolidVol();
    const volScalarField& alphaS = tAlphaS();
    const volScalarField& RRpor = tRRpor();

    scalarField allFluxScale(mesh_.nFaces(), 1.0);

    slicedSurfaceScalarField fluxScale
    (
        IOobject
        (
            "solidFluxLimiter",
            time_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE,
            false
        ),
        mesh_,
        dimless,
        allFluxScale,
        false               // slice the couples, so syncFaceList sees them
    );

    scalarField donorScale(mesh_.nCells(), 1.0);
    scalarField receiverScale(mesh_.nCells(), 1.0);

    for (label sweep = 0; sweep < nSolidFluxLimiterCorrectors_; ++sweep)
    {
        // Every sweep but the last credits flux crossing back the
        // other way, letting a jam travel up the bed in one step. The
        // final, uncredited sweep sets the bound that's applied.
        const bool credit = (sweep < nSolidFluxLimiterCorrectors_ - 1);

        solidDonorLimit(phiYm, RRsolid, fluxScale, credit, donorScale);

        solidReceiverLimit
        (
            phiSolidVol,
            alphaS,
            RRpor,
            fluxScale,
            credit,
            receiverScale
        );

        applySolidFaceLimit
        (
            phiYmTotal,
            donorScale,
            receiverScale,
            allFluxScale,
            fluxScale
        );
    }

    reportSolidFluxLimiter
    (
        phiYm,
        RRsolid,
        phiSolidVol,
        alphaS,
        RRpor,
        fluxScale
    );

    phiSolid *= fluxScale;

    return tPhiSolid;
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// ************************************************************************* //
