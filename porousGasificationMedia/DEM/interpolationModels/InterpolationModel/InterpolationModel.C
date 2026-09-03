#include "InterpolationModel.H"
#include "laplaceAnchored.H"
#include "laplaceSetValues.H"
#include "IOdictionary.H"

namespace Foam
{

template<class Type>
InterpolationModel<Type>::InterpolationModel
(
    const dictionary& dict,
    const fvMesh& mesh,
    GeometricField<Type, fvPatchField, volMesh>& fieldDEM,
    GeometricField<Type, fvPatchField, volMesh>& field,
    const volScalarField& nParticles,
    const volScalarField& porosityF,
    const Type& backgroundValue
)
:
    mesh_(mesh),
    fieldDEM_(fieldDEM),
    field_(field),
    nParticles_(nParticles),
    porosityF_(porosityF),
    solidPorosityCutoff_(0.0),
    backgroundValue_(backgroundValue)
{
    (void)dict;

    // criticalPorosity lives in pyrolysisProperties, not lambdaDict, and is
    // shared with lambdaDotModel so the Us and lambda interpolation models
    // agree with it on where the solid region ends.
    IOdictionary pyrolysisProperties
    (
        IOobject
        (
            "pyrolysisProperties",
            mesh.time().constant(),
            mesh,
            IOobject::MUST_READ,
            IOobject::NO_WRITE
        )
    );

    solidPorosityCutoff_ =
        readScalar
        (
            pyrolysisProperties
                .subDict("pyrolysisCoeffs")
                .lookup("criticalPorosity")
        );

    Info<< "criticalPorosity = " << solidPorosityCutoff_ << nl << endl;
}


template<class Type>
dictionary InterpolationModel<Type>::solverControls
(
    const word& solverName
) const
{
    if (const dictionary* dictPtr = mesh_.solversDict().findDict(solverName))
    {
        return *dictPtr;
    }

    // Same controls as the "Us" entry the DEM cases carry for the velocity
    // interpolation solve.
    dictionary controls;
    controls.add("solver", word("smoothSolver"));
    controls.add("smoother", word("symGaussSeidel"));
    controls.add("tolerance", scalar(1e-6));
    controls.add("relTol", scalar(0.01));

    return controls;
}


template<class Type>
autoPtr<InterpolationModel<Type>> InterpolationModel<Type>::New
(
    const word& modeKey,
    const dictionary& dict,
    const fvMesh& mesh,
    GeometricField<Type, fvPatchField, volMesh>& fieldDEM,
    GeometricField<Type, fvPatchField, volMesh>& field,
    const volScalarField& nParticles,
    const volScalarField& porosityF,
    const Type& backgroundValue
)
{
    // Select the fieldDEM-to-field interpolation method: laplaceAnchored
    // keeps a soft anchored diffusion solve, laplaceSetValues uses a hard
    // setValues Laplace solve. modeKey is passed in (rather than hardcoded)
    // so Us and lambda dispatch on their own dictionary keys and can be
    // smoothed differently within one lambdaDict.
    const word modelName
    (
        dict.lookupOrDefault<word>(modeKey, "laplaceSetValues")
    );

    Info<< "Selecting " << field.name() << " interpolation model: "
        << modelName << nl << endl;

    if (modelName == "laplaceAnchored")
    {
        return autoPtr<InterpolationModel<Type>>
        (
            new LaplaceAnchoredInterpolation<Type>
            (
                dict,
                mesh,
                fieldDEM,
                field,
                nParticles,
                porosityF,
                backgroundValue
            )
        );
    }
    else if (modelName == "laplaceSetValues")
    {
        return autoPtr<InterpolationModel<Type>>
        (
            new LaplaceSetValuesInterpolation<Type>
            (
                dict,
                mesh,
                fieldDEM,
                field,
                nParticles,
                porosityF,
                backgroundValue
            )
        );
    }

    FatalErrorInFunction
        << "Unknown " << modeKey << " '" << modelName << "'." << nl
        << "Valid options are: laplaceAnchored, laplaceSetValues"
        << exit(FatalError);

    return autoPtr<InterpolationModel<Type>>();
}

} // namespace Foam

// The only two fields that use this model are the vector solid velocity
// (Us) and the scalar DEM particle length scale (lambda).
template class Foam::InterpolationModel<Foam::vector>;
template class Foam::InterpolationModel<Foam::scalar>;
