#ifndef noneLambdaDot_H
#define noneLambdaDot_H

#include "LambdaDotCalculationModel.H"

namespace Foam
{

// Explicit zero-lambdaDot model: the particle length scale never changes.
// Use lambdaMode none to switch the split off unambiguously.
class noneLambdaDot
:
    public LambdaDotCalculationModel
{
public:
    noneLambdaDot
    (
        const dictionary& dict,
        const fvMesh& mesh,
        volScalarField& lambdaDot
    );

    virtual ~noneLambdaDot() = default;

    virtual void calculateTemperatureDriven() override;
};

} // namespace Foam

#endif
