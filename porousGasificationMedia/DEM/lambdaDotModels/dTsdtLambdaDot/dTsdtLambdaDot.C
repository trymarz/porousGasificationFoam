#include "dTsdtLambdaDot.H"

namespace Foam
{

dTsdtLambdaDot::dTsdtLambdaDot
(
    const dictionary& dict,
    const fvMesh& mesh,
    volScalarField& lambdaDot
)
:
    LambdaDotCalculationModel(dict, mesh, lambdaDot),
    lambdaFunc_(Function1<scalar>::New("lambdaDot", dict, &mesh)),
    TsForLambdaOld_(),
    lambdaDeltaT_(0.0),
    haveTsForLambdaOld_(false)
{}


void dTsdtLambdaDot::calculate()
{
    const volScalarField& TsField =
        mesh_.lookupObject<volScalarField>("Ts");

    const scalarField& Ts = TsField.primitiveField();

    // updateLambdaDot() is called before Ts is solved for the current step.
    //   oldTime() is not useful here. This model computes
    // the rate from the last completed thermal step:
    //     dTsdt = (Ts^n - Ts^(n-1))/deltaT^n
    if (!haveTsForLambdaOld_ || TsForLambdaOld_.size() != Ts.size())
    {
        TsForLambdaOld_ = Ts;
        lambdaDeltaT_ = mesh_.time().deltaTValue();
        haveTsForLambdaOld_ = true;

        // No completed temperature increment exists on the first call.
        lambdaDot_ = 0.0;
        return;
    }

    const scalar dt = max(lambdaDeltaT_, VSMALL);

    forAll(lambdaDot_, cellI)
    {
        const scalar dTsdt = (Ts[cellI] - TsForLambdaOld_[cellI])/dt;
        lambdaDot_[cellI] = lambdaFunc_->value(dTsdt);
    }

    // Store current Ts for the next coupling call.
    TsForLambdaOld_ = Ts;
    lambdaDeltaT_ = mesh_.time().deltaTValue();
}

} // namespace Foam
