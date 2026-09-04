#ifndef exactDifferentialLambdaDot_H
#define exactDifferentialLambdaDot_H

#include "LambdaDotCalculationModel.H"
#include "volFields.H"

namespace Foam
{

// lambdaDot as the total derivative of the DEM particle length scale
// lambda [m] in solid temperature Ts and specie mass concentrations Ym_i:
//
//   dlambda/dt = dlambdaOverDTs*(dTs/dt)
//              + splitMassBetweenLamAndPor*sum_i[dlambdaOverDYmi_i*sRhoSi_i]
//
// Coefficients are calibration inputs from constant/lambdaDict, each
// defaulting to 0.0 (so an unconfigured case gives lambdaDot = 0 and sends
// all mass to porosity):
//
//   dlambdaOverDTs             [m/K]
//   dlambdaOverDYmi            [m^4/kg]  one scalar, or a per-specie subdict
//   splitMassBetweenLamAndPor  [-]       in [0,1]
//
// dlambdaOverDYmi is dimLength/dimDensity, so
// [m^4/kg]*[kg/m^3/s] = [m/s] = lambdaDot.
//
// splitMassBetweenLamAndPor scales only the chemistry sum; volPyrolysis
// feeds the remaining fraction into porosity. It splits the attribution, not
// a volume -- the shrinkage magnitude is dlambdaOverDYmi's alone -- and never
// scales the mass change itself, which Ym transport consumes in full.
//
// calculateTemperatureDriven() sets the temperature term once per step;
// calculateChemistryDriven() accumulates the chemistry term per specie.
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

    //- Fraction of the chemistry mass change attributed to shrinkage rather
    //  than to pore space. In [0,1].
    scalar splitMassBetweenLamAndPor_;

    //- Ts snapshot and deltaT from the last completed thermal step, which
    //  dTs/dt is differenced over. Not yet set on the first call.
    volScalarField TsForLambdaOld_;
    scalar lambdaDeltaT_;
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
