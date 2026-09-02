#ifndef exactDifferentialLambdaDot_H
#define exactDifferentialLambdaDot_H

#include "LambdaDotCalculationModel.H"
#include "volFields.H"

namespace Foam
{

// Exact-differential lambda/porosity split. lambdaDot is the total derivative
// of the DEM particle length scale lambda [m] with respect to the two state
// variables it is taken to depend on — the solid temperature Ts and the solid
// specie mass concentrations Ym_i:
//
//   dlambda/dt = (dlambda/dTs)*(dTs/dt)
//              + splitMassBetweenLamAndPor
//                * sum_i[ (dlambda/dYm_i)*(dYm_i/dt) ]
//
//   dYm_i/dt = RRs(i) = sRhoSi   [kg/m^3/s]
//
// Both partial derivatives are free coefficients read from
// constant/lambdaDict, so the split is a calibration input rather than a
// geometric assumption:
//
//   dlambdaOverDTs   [m/K]         scalar
//   dlambdaOverDYmi  [m^4/kg]      scalar, or a subdict of per-specie scalars
//
// dlambdaOverDYmi carries dimLength/dimDensity because sRhoSi is a density
// rate: [m]/([kg/m^3]) = [m^4/kg], and [m^4/kg]*[kg/m^3/s] = [m/s] = the
// dimensions of lambdaDot.
//
// splitMassBetweenLamAndPor [-] scales only the chemistry sum: that fraction
// of the chemistry mass change is taken as particle shrinkage, and
// volPyrolysis feeds the remaining (1 - splitMassBetweenLamAndPor) share into
// its porosity source, so the two consumers of one mass change stay
// consistent. The mass change itself is never scaled — the Ym transport in
// volPyrolysis consumes the full, unmodified sRhoSi.
//
// The temperature-driven part is set once per step by
// calculateTemperatureDriven(); the chemistry-driven part is accumulated per
// solid specie by calculateChemistryDriven() inside volPyrolysis's species
// loop. Every coefficient defaults to 0.0, so an unconfigured case gives
// lambdaDot = 0 exactly and all chemistry mass loss becomes porosity — the
// pre-feature behaviour, and the reason no separate "off" mode exists.
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
