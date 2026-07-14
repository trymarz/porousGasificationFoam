#ifndef exactDifferentialLambdaDot_H
#define exactDifferentialLambdaDot_H

#include "LambdaDotCalculationModel.H"
#include "scalarField.H"

namespace Foam
{

// Exact-differential lambda/porosity split. lambdaDot is the total derivative
// of the particle length scale, assembled from two contributions:
//
//   * temperature-driven:  lambdaDot  = dlambdaOverDTs * dTs/dt
//   * chemistry-driven:     lambdaDot += massSplitBetweenLamAndPor
//                                        * V/(3*rho*lambda^2) * sRhoSi
//
// The temperature-driven part is set once per step by
// calculateTemperatureDriven(); the chemistry-driven part is accumulated per
// solid specie by calculateChemistryDriven() inside volPyrolysis's species
// loop. Both coefficients are read from constant/solidThermophysicalProperties
// and default to 0.0, so an unconfigured case reproduces the pre-split
// behaviour (lambdaDot = 0). This model supersedes the earlier dTsdtLambdaDot,
// of which it is a strict superset.
class exactDifferentialLambdaDot
:
    public LambdaDotCalculationModel
{
private:
    //- Temperature coefficient dlambda/dTs [m/K].
    scalar dlambdaOverDTs_;

    //- Fraction of the chemistry mass change redirected into the particle
    //  length scale; the remaining (1 - massSplitBetweenLamAndPor) becomes
    //  pore space. In [0,1].
    scalar massSplitBetweenLamAndPor_;

    //- Ts snapshot from the previous completed thermal step.
    scalarField TsForLambdaOld_;

    //- deltaT used by the last completed thermal step.
    scalar lambdaDeltaT_;

    //- First call has no completed Ts increment yet.
    bool haveTsForLambdaOld_;

public:
    exactDifferentialLambdaDot
    (
        const dictionary& dict,
        const fvMesh& mesh,
        volScalarField& lambdaDot
    );

    virtual ~exactDifferentialLambdaDot() = default;

    virtual void calculateTemperatureDriven() override;

    virtual void calculateChemistryDriven
    (
        const volScalarField& sRhoSi,
        const volScalarField& rho,
        const volScalarField& lambda
    ) override;

    virtual scalar chemistryMassSplit() const override
    {
        return massSplitBetweenLamAndPor_;
    }
};

} // namespace Foam

#endif
