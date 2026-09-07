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
// splitMassBetweenLamAndPor scales this chemistry sum, and volPyrolysis
// withholds that same fraction from the porosity source it would otherwise
// assign the unsplit rate -- one coefficient, read in both places, splitting
// one mass-loss rate between the two channels. It never touches the mass
// change itself, which Ym transport consumes in full regardless; only how
// that rate counts afterwards -- toward shrinkage or toward pore-opening --
// is split. The resulting shrinkage magnitude also depends on
// dlambdaOverDYmi, an independently calibrated conversion, so there is no
// volume-conservation law tying the particle-volume change to the pore
// volume withheld -- only the rate split itself is exact.
//
// Physically, lambda and porosity are orthogonal state variables: each
// responds to its own share of the split rate (both in turn driven by Ts
// and Ym_i), and changing one does not itself change the other. Porosity
// rising means the cell's void volume grows; lambda shrinking means the
// imaginary DEM particle itself gets smaller -- two independent numbers,
// not one derived from the other.
//
// An informal mental picture for telling them apart: porosity growth is
// like pore space opening up throughout the particle's volume, while
// lambda shrinkage is more like mass leaving from the particle's outer
// surface, compacting solid toward its centre and pushing the void to the
// outside. This sub-particle picture is descriptive only, not something
// the code represents: the well-stirred-reactor approximation this solver
// uses keeps lambda and porosity as one scalar each per FVM cell, with no
// radial structure inside the particle, so nothing here actually
// distinguishes "pores throughout the particle" from "mass removed from
// its surface" -- both pictures collapse to the same two independent
// per-cell rates.
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
