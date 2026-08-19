#ifndef LambdaDotCalculationModel_H
#define LambdaDotCalculationModel_H

#include "fvMesh.H"
#include "volFields.H"
#include "dictionary.H"
#include "autoPtr.H"

namespace Foam
{

class LambdaDotCalculationModel
{
protected:
    const fvMesh& mesh_;
    volScalarField& lambdaDot_;

public:
    LambdaDotCalculationModel
    (
        const dictionary& dict,
        const fvMesh& mesh,
        volScalarField& lambdaDot
    );

    virtual ~LambdaDotCalculationModel() = default;

    static autoPtr<LambdaDotCalculationModel> New
    (
        const dictionary& dict,
        const fvMesh& mesh,
        volScalarField& lambdaDot
    );

    // Calculate lambdaDot for the current coupling call.
    virtual void calculate() = 0;
};

} // namespace Foam

#endif
