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

#include "solidDiagnostics.H"

#include "PstreamReduceOps.H"
#include "fvcVolumeIntegrate.H"
#include "fixedValueFvPatchFields.H"
#include "DynamicList.H"
#include "wallPolyPatch.H"

#include <cmath>

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //

label SolidDiagnostics::firstInvalidCell
(
    const volScalarField& fld,
    const scalar lower,
    const scalar upper,
    const scalarField* mask
) const
{
    forAll(fld, cellI)
    {
        if (mask && (*mask)[cellI] <= 0.0)
        {
            continue;
        }

        const scalar value = fld[cellI];

        if (!std::isfinite(value) || value < lower || value > upper)
        {
            return cellI;
        }
    }

    return -1;
}


scalar SolidDiagnostics::totalYmMass() const
{
    scalar total = 0.0;
    for (label i = 0; i < Ym_.size(); ++i)
    {
        total += gSum(Ym_[i].field() * mesh_.V());
    }
    return total;
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

SolidDiagnostics::SolidDiagnostics
(
    const dictionary& coeffs,
    const SolidFields& solid
)
:
    mesh_(solid.mesh),
    time_(solid.mesh.time()),
    failOnInvalidSolidState_
    (
        coeffs.lookupOrDefault("failOnInvalidSolidState",true)
    ),
    solidState_(solid.state),
    Ym_(solid.Ym),
    Ys_(solid.Ys),
    chemistry_(solid.chemistry),
    initialYmMass_(totalYmMass()),
    lostSolidMass_(dimensionedScalar("zero", dimMass, 0.0)),
    addedGasMass_(dimensionedScalar("zero", dimMass, 0.0)),
    totalGasMassFlux_(dimensionedScalar("zero", dimMass/dimTime, 0.0)),
    totalHeatRR_(dimensionedScalar("zero", dimEnergy/dimTime, 0.0)),
    maxSolidTemperature_
    (
        coeffs.lookupOrDefault<scalar>("maxSolidTemperature",1e5)
    ),
    solidBoundaryPolicy_
    (
        coeffs.lookupOrDefault<word>("solidBoundaryPolicy", "error")
    ),
    inheritingPatches_(),
    declaringPatches_(),
    wallPatches_(),
    inflowReported_(),
    outflowReported_()
{
    if
    (
        solidBoundaryPolicy_ != "error"
     && solidBoundaryPolicy_ != "warn"
     && solidBoundaryPolicy_ != "ignore"
    )
    {
        FatalIOErrorInFunction(coeffs)
            << "solidBoundaryPolicy " << solidBoundaryPolicy_
            << " is not one of error, warn, ignore" << exit(FatalIOError);
    }

    Info << "failOnInvalidSolidState  " << failOnInvalidSolidState_ << endl;
    Info << "maxSolidTemperature      " << maxSolidTemperature_ << endl;
    Info << "solidBoundaryPolicy        " << solidBoundaryPolicy_ << endl;
}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

SolidDiagnostics::~SolidDiagnostics()
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

label SolidDiagnostics::firstInvalidPorosity
(
    const volScalarField& por
) const
{
    if (!failOnInvalidSolidState_)
    {
        return -1;
    }

    return firstInvalidCell
    (
        por,
        solidState_.minPorosity() - solidState_.checkTolerance(),
        1.0 + solidState_.checkTolerance()
    );
}


label SolidDiagnostics::firstInvalidExtensive
(
    const volScalarField& fld
) const
{
    if (!failOnInvalidSolidState_)
    {
        return -1;
    }

    const scalar fldScale = max(gMax(fld), SMALL);

    return firstInvalidCell(fld, -solidState_.checkTolerance()*fldScale, GREAT);
}


label SolidDiagnostics::firstInvalidTemperature
(
    const volScalarField& T,
    const scalarField& mask
) const
{
    if (!failOnInvalidSolidState_)
    {
        return -1;
    }

    return firstInvalidCell
    (
        T,
       -solidState_.checkTolerance()*maxSolidTemperature_,
        maxSolidTemperature_,
        &mask
    );
}


label SolidDiagnostics::firstInvalidTemperature
(
    const volScalarField& T
) const
{
    if (!failOnInvalidSolidState_)
    {
        return -1;
    }

    return firstInvalidCell(T, 0.0, maxSolidTemperature_);
}


void SolidDiagnostics::abort
(
    const word& stage,
    const volScalarField& fld,
    const label cellI,
    const string& context
) const
{
    FatalErrorInFunction
        << "Impossible solid state produced by " << stage << nl << nl
        << "    time         = " << time_.timeName() << nl
        << "    deltaT       = " << time_.deltaTValue() << nl
        << "    field        = " << fld.name() << nl
        << "    value        = " << fld[cellI] << nl
        << "    processor    = " << Pstream::myProcNo() << nl
        << "    cell         = " << cellI << nl
        << "    cell centre  = " << mesh_.C()[cellI] << nl
        << "    cell volume  = " << mesh_.V()[cellI] << nl
        << context.c_str() << nl
        << "The solid transport equations are explicit and unbounded. Either"
        << " the solid velocity Us is invalid, or the explicit update has"
        << " overshot at this time step." << nl
        << exit(FatalError);
}


void SolidDiagnostics::checkConsistency
(
    const volScalarField& porosity,
    const PtrList<volScalarField>& Ym,
    const volScalarField& rho
) const
{
    scalar maxResidual = 0.0;
    label worstCell = -1;

    forAll(porosity, cellI)
    {
        scalar cellYm = 0.0;
        forAll(Ym, i)
        {
            cellYm += Ym[i][cellI];
        }

        const scalar residual = mag
        (
            1.0 - porosity[cellI] - cellYm/max(rho[cellI], SMALL)
        );

        if (residual > maxResidual)
        {
            maxResidual = residual;
            worstCell = cellI;
        }
    }

    const scalar globalResidual =
        returnReduce(maxResidual, maxOp<scalar>());

    Info<< "solid state consistency: max|1 - porosity"
        << " - sum(Ym_i/rho_i)| = " << globalResidual << endl;

    // Last two conditions: only the rank owning the worst cell reports it.
    if
    (
        globalResidual > solidState_.checkTolerance()
     && worstCell != -1
     && maxResidual == globalResidual
    )
    {
        WarningInFunction
            << "porosity and solid mass disagree by " << maxResidual
            << " in cell " << worstCell << " at "
            << mesh_.C()[worstCell] << ": porosity = "
            << porosity[worstCell] << ", solidPresent = "
            << solidState_.solidPresent()[worstCell]
            << ". A porosity written after the recovery cannot be"
            << " reconciled with the mass the cell holds." << endl;
    }
}


void SolidDiagnostics::reportMassBudget
(
    const scalar clipped,
    const scalar flipped,
    const scalar collapsed
) const
{
    Info<< "solid mass budget: sum(Ym_i * V) = " << totalYmMass()
        << ", initial = " << initialYmMass_
        << ", fabricated by clip = " << clipped
        << ", destroyed by flip = " << flipped
        << ", destroyed by collapse = " << collapsed
        << endl;
}


void SolidDiagnostics::reportDerivedSpecies() const
{
    for (label i = 0; i < Ys_.size(); ++i)
    {
        Info<< "       derived " << Ys_[i].name()
            << " min/max = " << gMin(Ys_[i]) << " / " << gMax(Ys_[i]) << endl;
    }
}


void SolidDiagnostics::accumulateRunTotals(const volScalarField& chemistrySh)
{
    totalGasMassFlux_ = fvc::domainIntegrate(chemistry_.RRg());
    totalHeatRR_ = fvc::domainIntegrate(chemistrySh);

    addedGasMass_ += fvc::domainIntegrate(chemistry_.RRg()) * time_.deltaT();
    lostSolidMass_ += fvc::domainIntegrate(chemistry_.RRs()) * time_.deltaT();
}


void SolidDiagnostics::reportRunTotals(const scalar replenished) const
{
    Info<< indent << "Total gas mass produced  [kg] = " << addedGasMass_.value() << nl
        << indent << "Total solid mass lost    [kg] = " << lostSolidMass_.value() << nl
        << indent << "Total mass replenished   [kg] = " << replenished << nl
        << indent << "Realese rate of pyrolysis gases  [kg/s] = " << totalGasMassFlux_.value() << nl
        << indent << "Heat release rate [J/s] = " << totalHeatRR_.value() << nl;
}


void SolidDiagnostics::reportTemperatureRange(const volScalarField& T) const
{
    scalar minTemp = GREAT;
    scalar maxTemp = -GREAT;
    scalar areThere = 0;
    forAll(T,cellI)
    {
        if (solidState_.solidPresent()[cellI] == 1 )
        {
            areThere = 1;
            if (T[cellI] < minTemp)
            {
                minTemp = T[cellI];
            }
            if (T[cellI] > maxTemp)
            {
                maxTemp = T[cellI];
            }
        }
    }
    reduce(maxTemp, maxOp<scalar>());
    reduce(minTemp, minOp<scalar>());
    reduce(areThere, maxOp<scalar>());
    if (areThere == 1)
    {
        Info<< " pyrolysis min/max(T) = " << minTemp << ", " << maxTemp << endl;
    }
    else
    {
        Info<< " no solid phase " << endl;
    }
}


void SolidDiagnostics::checkBoundaryDeclarations
(
    const PtrList<volScalarField>& Ys,
    const volScalarField& Ts
)
{
    DynamicList<label> inheriting;
    DynamicList<label> declaring;
    DynamicList<label> walls;

    forAll(mesh_.boundary(), patchi)
    {
        const polyPatch& pp = mesh_.boundaryMesh()[patchi];

        if (polyPatch::constraintType(pp.type()))
        {
            continue;
        }

        if (isWall(patchi))
        {
            walls.append(patchi);
        }

        label nFixed = 0;
        forAll(Ys, i)
        {
            if
            (
                isA<fixedValueFvPatchScalarField>
                (
                    Ys[i].boundaryField()[patchi]
                )
            )
            {
                ++nFixed;
            }
        }

        if (nFixed == 0)
        {
            inheriting.append(patchi);
            continue;
        }

        if (nFixed < Ys.size())
        {
            FatalErrorInFunction
                << "Patch " << pp.name() << " fixes " << nFixed << " of "
                << Ys.size() << " solid mass fractions, so the composition"
                << " of solid entering it is ambiguous." << nl
                << "    Fix every solid Yi on it (an inlet) or none"
                << " (a wall or an outlet)." << exit(FatalError);
        }

        if (!isA<fixedValueFvPatchScalarField>(Ts.boundaryField()[patchi]))
        {
            FatalErrorInFunction
                << "Patch " << pp.name() << " fixes the solid composition"
                << " but Ts is " << Ts.boundaryField()[patchi].type()
                << ", so solid entering it takes the face cell's enthalpy."
                << nl << "    Make Ts fixedValue at the inlet temperature"
                << " (an inlet), or every solid Yi zeroGradient (a wall)."
                << exit(FatalError);
        }

        declaring.append(patchi);

        // Faces summing to zero declare no solid; checkSolidCrossing() acts
        // only if solid enters them, so an inlet may be fed later.
        scalarField sumY(pp.size(), 0.0);
        forAll(Ys, i)
        {
            sumY += Ys[i].boundaryField()[patchi];
        }
        scalar minSum = GREAT;
        scalar maxSum = -GREAT;
        forAll(sumY, facei)
        {
            if (sumY[facei] > SMALL)
            {
                minSum = min(minSum, sumY[facei]);
                maxSum = max(maxSum, sumY[facei]);
            }
        }
        reduce(minSum, minOp<scalar>());
        reduce(maxSum, maxOp<scalar>());

        if
        (
            minSum <= maxSum
         && (mag(minSum - 1.0) > 1e-6 || mag(maxSum - 1.0) > 1e-6)
        )
        {
            WarningInFunction
                << "The fixed solid Yi on patch " << pp.name()
                << " sum to " << minSum << " .. " << maxSum
                << "; they are normalised to 1." << endl;
        }
    }

    inheritingPatches_.transfer(inheriting);
    declaringPatches_.transfer(declaring);
    wallPatches_.transfer(walls);
    inflowReported_.setSize(mesh_.boundary().size(), false);
    outflowReported_.setSize(mesh_.boundary().size(), false);

    Info<< "solid boundary: declaring " << patchNames(declaringPatches_)
        << ", inheriting " << patchNames(inheritingPatches_)
        << ", closed walls " << patchNames(wallPatches_) << endl;
}


wordList SolidDiagnostics::patchNames(const labelList& patches) const
{
    wordList names(patches.size());
    forAll(patches, k)
    {
        names[k] = mesh_.boundaryMesh()[patches[k]].name();
    }
    return names;
}


bool SolidDiagnostics::beyondSlack(const scalar phi, const label celli) const
{
    return
        phi*time_.deltaTValue()
      > solidState_.checkTolerance()*mesh_.V()[celli];
}


bool SolidDiagnostics::isWall(const label patchi) const
{
    return isA<wallPolyPatch>(mesh_.boundaryMesh()[patchi]);
}


scalarField SolidDiagnostics::patchTotalYm
(
    const PtrList<volScalarField>& Ym,
    const label patchi
) const
{
    scalarField YmP(mesh_.boundary()[patchi].size(), 0.0);
    forAll(Ym, i)
    {
        YmP += Ym[i].boundaryField()[patchi];
    }
    return YmP;
}


void SolidDiagnostics::applyBoundaryPolicy
(
    const string& what,
    const string& remedy
)
{
    if (solidBoundaryPolicy_ == "error")
    {
        FatalErrorInFunction
            << what.c_str() << nl << "    " << remedy.c_str()
            << ", or set solidBoundaryPolicy warn or ignore."
            << exit(FatalError);
    }

    WarningInFunction << what.c_str() << " Reported once." << endl;
}


void SolidDiagnostics::checkSolidCrossing
(
    const surfaceScalarField& phiSolid,
    const PtrList<volScalarField>& Ys,
    const PtrList<volScalarField>& Ym
)
{
    if (solidBoundaryPolicy_ == "ignore")
    {
        return;
    }

    // Walls are checked in both directions below.
    forAll(inheritingPatches_, k)
    {
        const label patchi = inheritingPatches_[k];
        if (inflowReported_[patchi] || isWall(patchi))
        {
            continue;
        }

        const scalarField& phiP = phiSolid.boundaryField()[patchi];
        const labelUList& faceCells = mesh_.boundary()[patchi].faceCells();
        const scalarField YmP(patchTotalYm(Ym, patchi));

        // A trace or empty face cell has no mass worth copying in.
        scalar inflow = 0.0;
        forAll(phiP, facei)
        {
            const label celli = faceCells[facei];
            if
            (
                beyondSlack(-phiP[facei], celli)
             && !solidState_.isTraceOrEmpty(celli)
            )
            {
                inflow -= phiP[facei]*YmP[facei];
            }
        }

        reduce(inflow, sumOp<scalar>());
        if (inflow > 0.0)
        {
            inflowReported_[patchi] = true;
            const word& name = mesh_.boundaryMesh()[patchi].name();
            applyBoundaryPolicy
            (
                "Solid enters through patch " + name + " ("
              + Foam::name(inflow)
              + " kg/s at t = " + time_.timeName() + "), which inherits the"
                " solid state: it copies the face cell's mass and enthalpy in.",
                "Fix every solid Yi and Ts on " + name
              + " to declare what enters"
            );
        }
    }

    forAll(declaringPatches_, k)
    {
        const label patchi = declaringPatches_[k];
        if (inflowReported_[patchi] || isWall(patchi))
        {
            continue;
        }

        const scalarField& phiP = phiSolid.boundaryField()[patchi];
        const labelUList& faceCells = mesh_.boundary()[patchi].faceCells();

        scalarField sumY(phiP.size(), 0.0);
        forAll(Ys, i)
        {
            sumY += Ys[i].boundaryField()[patchi];
        }

        scalar inflow = 0.0;
        forAll(phiP, facei)
        {
            if
            (
                sumY[facei] <= SMALL
             && beyondSlack(-phiP[facei], faceCells[facei])
            )
            {
                inflow -= phiP[facei];
            }
        }

        reduce(inflow, sumOp<scalar>());
        if (inflow > 0.0)
        {
            inflowReported_[patchi] = true;
            const word& name = mesh_.boundaryMesh()[patchi].name();
            applyBoundaryPolicy
            (
                "Solid enters through patch " + name + " ("
              + Foam::name(inflow)
              + " m3/s at t = " + time_.timeName() + ") on faces whose fixed"
                " solid Yi sum to zero: fixedSolidH brings enthalpy in"
                " without mass.",
                "Fix a non-zero solid Yi on " + name + " while solid enters it,"
                " or stop Us there"
            );
        }
    }

    // Solid copied in from a trace or empty cell carries no mass, as above.
    forAll(wallPatches_, k)
    {
        const label patchi = wallPatches_[k];
        const scalarField& phiP = phiSolid.boundaryField()[patchi];
        const labelUList& faceCells = mesh_.boundary()[patchi].faceCells();
        const scalarField YmP(patchTotalYm(Ym, patchi));
        const bool declares =
            isA<fixedValueFvPatchScalarField>(Ys[0].boundaryField()[patchi]);

        scalar inflow = 0.0;
        scalar outflow = 0.0;
        forAll(phiP, facei)
        {
            const label celli = faceCells[facei];
            const bool holdsSolid = !solidState_.isTraceOrEmpty(celli);
            if (beyondSlack(-phiP[facei], celli) && (declares || holdsSolid))
            {
                inflow -= phiP[facei];
            }
            if (beyondSlack(phiP[facei], celli) && holdsSolid)
            {
                outflow += phiP[facei]*YmP[facei];
            }
        }
        reduce(inflow, sumOp<scalar>());
        reduce(outflow, sumOp<scalar>());

        const word& name = mesh_.boundaryMesh()[patchi].name();
        if (inflow > 0.0 && !inflowReported_[patchi])
        {
            inflowReported_[patchi] = true;
            applyBoundaryPolicy
            (
                "Solid enters through wall " + name + " ("
              + Foam::name(inflow)
              + " m3/s at t = " + time_.timeName() + "): its Us lets the"
                " solid cross it.",
                "Declare " + name + " as type patch if it is an inlet, or"
                " make its Us slip or fixedValue (0 0 0) to close it"
            );
        }
        if (outflow > 0.0 && !outflowReported_[patchi])
        {
            outflowReported_[patchi] = true;
            applyBoundaryPolicy
            (
                "Solid leaves through wall " + name + " ("
              + Foam::name(outflow)
              + " kg/s at t = " + time_.timeName() + "): its Us lets the"
                " solid cross it.",
                "Declare " + name + " as type patch if it is an outlet, or"
                " make its Us slip or fixedValue (0 0 0) to close it"
            );
        }
    }
}

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// ************************************************************************* //
