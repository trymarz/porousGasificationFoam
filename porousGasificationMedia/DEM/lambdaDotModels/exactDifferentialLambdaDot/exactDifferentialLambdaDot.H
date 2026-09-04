#ifndef exactDifferentialLambdaDot_H
#define exactDifferentialLambdaDot_H

#include "LambdaDotCalculationModel.H"
#include "volFields.H"

namespace Foam
{

// Exact-differential lambda/porosity split. lambdaDot is the total
// derivative of the DEM particle length scale lambda [m] w.r.t. the solid
// temperature Ts and specie mass concentrations Ym_i:
//
//   dlambda/dt = (dlambda/dTs)*(dTs/dt)
//              + splitMassBetweenLamAndPor
//                * sum_i[ (dlambda/dYm_i)*(dYm_i/dt) ],   dYm_i/dt = sRhoSi
//
// Both partials are free coefficients from constant/lambdaDict (a
// calibration input, not a geometric assumption):
//
//   dlambdaOverDTs   [m/K]     scalar
//   dlambdaOverDYmi  [m^4/kg]  scalar, or a per-specie subdict
//
// dlambdaOverDYmi carries dimLength/dimDensity: [m]/[kg/m^3] = [m^4/kg], and
// [m^4/kg]*[kg/m^3/s] = [m/s] = lambdaDot's dimensions.
//
// splitMassBetweenLamAndPor [-] scales only the chemistry sum -- that
// fraction of the mass change becomes particle shrinkage, volPyrolysis feeds
// the rest into porosity, and the mass change itself is never scaled (Ym
// transport still consumes the full sRhoSi), so the two consumers stay
// consistent.
//
// calculateTemperatureDriven() sets the temperature term once per step;
// calculateChemistryDriven() accumulates the chemistry term per specie.
// Every coefficient defaults to 0.0, so an unconfigured case reduces to the
// pre-feature behaviour (lambdaDot = 0, all mass to porosity) -- why there
// is no separate "off" mode.
class exactDifferentialLambdaDot
:
    public LambdaDotCalculationModel
{
private:
    //- Temperature coefficient dlambda/dTs [m/K].
    dimensionedScalar dlambdaOverDTs_;

    //- True when dlambdaOverDYmi was given as one scalar for every specie,
    //  false when it was given as a per-specie subdict.
    bool dlambdaOverDYmiUniform_;

    //- The single dlambdaOverDYmi value [m^4/kg], used when uniform.
    scalar dlambdaOverDYmiUniformValue_;

    //- Per-specie dlambdaOverDYmi values [m^4/kg], keyed by the bare
    //  solidComponents name. A specie absent from the subdict reads 0.0.
    dictionary dlambdaOverDYmiPerSpecie_;

    //- Fraction of the chemistry mass change redirected into the particle
    //  length scale; the remaining (1 - splitMassBetweenLamAndPor) becomes
    //  pore space. In [0,1].
    scalar splitMassBetweenLamAndPor_;

    //- Ts snapshot from the previous completed thermal step.
    volScalarField TsForLambdaOld_;

    //- deltaT used by the last completed thermal step.
    scalar lambdaDeltaT_;

    //- First call has no completed Ts increment yet.
    bool haveTsForLambdaOld_;

    //- Resolve dlambdaOverDYmi [m^4/kg] for one specie.
    scalar dlambdaOverDYmi(const word& specieName) const;

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
        const word& specieName
    ) override;

    virtual scalar chemistryMassSplit() const override
    {
        return splitMassBetweenLamAndPor_;
    }
};

} // namespace Foam

#endif
