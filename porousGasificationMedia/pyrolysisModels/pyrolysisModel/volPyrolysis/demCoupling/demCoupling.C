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

#include "demCoupling.H"

#include "IOdictionary.H"
#include "Switch.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace heterogeneousPyrolysisModels
{

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

DemCoupling::DemCoupling(const fvMesh& mesh)
:
    demActive_(false),
    lambdaDotPtr_(nullptr)
{
    // DEM counts as active only when both signals agree: yadeProperties asks
    // for it, and the lambdaDot/lambda fields exist. Either alone misleads --
    // a WITH_YADE solver registers the fields even with DEM off, and a
    // non-YADE solver can still read active=true.
    IOdictionary yadeProperties
    (
        IOobject
        (
            "yadeProperties",
            mesh.time().constant(),
            mesh,
            IOobject::READ_IF_PRESENT,
            IOobject::NO_WRITE,
            false // unregistered: the solver may own this object name
        )
    );

    const Switch demRequested
    (
        yadeProperties.lookupOrDefault<Switch>("active", false)
    );

    const bool demFieldsRegistered =
        mesh.foundObject<volScalarField>("lambdaDot")
     && mesh.foundObject<volScalarField>("lambda");

    demActive_ = demRequested && demFieldsRegistered;

    if (demActive_)
    {
        lambdaDotPtr_ =
            &mesh.lookupObjectRef<volScalarField>("lambdaDot");

#ifdef WITH_YADE
        // lambdaMode and its coefficients. Opened unregistered, as
        // lambdaDotModel reads the same dictionary for its interpolation
        // entries.
        IOdictionary lambdaDict
        (
            IOobject
            (
                "lambdaDict",
                mesh.time().constant(),
                mesh,
                IOobject::MUST_READ,
                IOobject::NO_WRITE,
                false
            )
        );

        lamDotCalc_ = LambdaDotCalculationModel::New
        (
            lambdaDict, mesh, *lambdaDotPtr_
        );
#endif
    }
    else if (demRequested)
    {
        WarningInFunction
            << "yadeProperties requests DEM coupling (active=true) but "
            << "the solver did not register the lambdaDot/lambda fields "
            << "(built without WITH_YADE?); "
            << "lambdaDot calculation disabled" << endl;
    }

    Info<< "volPyrolysis: DEM lambdaDot calculation "
        << (demActive_ ? "active" : "inactive") << endl;
}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

DemCoupling::~DemCoupling()
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

void DemCoupling::beginStep()
{
#ifdef WITH_YADE
    if (demActive_)
    {
        lamDotCalc_->beginStep();
        lamDotCalc_->calculateTemperatureDriven();
    }
#endif
}

void DemCoupling::addChemistryTerm
(
    const volScalarField& sRhoSi,
    const word& specie
)
{
    // A no-op under lambdaMode constant. The name is the bare
    // solidComponents one ("char", not "Ychar"), as dlambdaOverDYmi is keyed.
#ifdef WITH_YADE
    if (demActive_)
    {
        lamDotCalc_->calculateChemistryDriven
        (
            sRhoSi,
            specie
        );
    }
#endif
}

void DemCoupling::finishStep()
{
#ifdef WITH_YADE
    if (demActive_)
    {
        lambdaDotPtr_->correctBoundaryConditions();
    }
#endif
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace heterogeneousPyrolysisModels
} // End namespace Foam

// ************************************************************************* //
