#ifndef exactDifferentialLambdaDot_H
#define exactDifferentialLambdaDot_H

#include "LambdaDotCalculationModel.H"
#include "volFields.H"

namespace Foam
{

// Exact-differential lambda/porosity split. lambdaDot is the total derivative
// of the DEM particle length scale lambda [m], assembled from two
// contributions:
//
//   * temperature-driven:  lambdaDot  = dlambdaOverDTs * dTs/dt
//   * chemistry-driven:    lambdaDot += massSplitBetweenLamAndPor
//                                       * V/(3*rho*lambda^2) * sRhoSi
//
// The chemistry-driven term is the exact differential of the particle mass
// m = rho*lambda^3 at fixed rho: dm = 3*rho*lambda^2*dlambda, so a solid-mass
// rate sRhoSi [kg/m^3/s] over a cell of volume V [m^3] maps to
// dlambda/dt = V*sRhoSi/(3*rho*lambda^2), of which the fraction
// massSplitBetweenLamAndPor is taken as particle shrinkage; the remaining
// (1 - massSplitBetweenLamAndPor) becomes pore space in volPyrolysis's
// porosity source. dlambdaOverDTs [m/K] therefore carries the units of lambda
// per kelvin, and lambdaDot is [m/s].
//
// The temperature-driven part is set once per step by
// calculateTemperatureDriven(); the chemistry-driven part is accumulated per
// solid specie by calculateChemistryDriven() inside volPyrolysis's species
// loop. Both coefficients are read from constant/lambdaDict and default to
// 0.0, so an unconfigured case reproduces the pre-split behaviour
// (lambdaDot = 0). This model supersedes the earlier dTsdtLambdaDot, of which
// it is a strict superset: dTsdt applied an arbitrary Function1 to the same
// backward difference (Ts - TsOld)/dt that dlambdaOverDTs now scales
// linearly.
class exactDifferentialLambdaDot
:
    public LambdaDotCalculationModel
{
private:
    //- Temperature coefficient dlambda/dTs [m/K].
    dimensionedScalar dlambdaOverDTs_;

    //- Fraction of the chemistry mass change redirected into the particle
    //  length scale; the remaining (1 - massSplitBetweenLamAndPor) becomes
    //  pore space. In [0,1].
    scalar massSplitBetweenLamAndPor_;

    //- Ts snapshot from the previous completed thermal step.
    volScalarField TsForLambdaOld_;

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
