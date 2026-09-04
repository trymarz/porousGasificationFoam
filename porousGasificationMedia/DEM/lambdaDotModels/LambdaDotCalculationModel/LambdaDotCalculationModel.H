#ifndef LambdaDotCalculationModel_H
#define LambdaDotCalculationModel_H

#include "fvMesh.H"
#include "volFields.H"
#include "dictionary.H"
#include "autoPtr.H"

namespace Foam
{

// Strategy interface for the DEM particle length-scale rate lambdaDot [m/s].
// Owned by volPyrolysis, selected via lambdaMode in constant/lambdaDict, and
// driven in solveSpeciesMass(): calculateTemperatureDriven() once per step,
// then calculateChemistryDriven() per solid specie. PGF owns only the rate;
// YADE integrates lambda itself (State::lambda_) and no model here reads it.
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

    //- Overwrites lambdaDot_ with the temperature-driven contribution.
    //  Called once per step, before the species loop.
    virtual void calculateTemperatureDriven() = 0;

    //- Adds this specie's mass-rate sRhoSi [kg/m^3/s] contribution to
    //  lambdaDot_. specieName is the bare solidComponents name ("char", not
    //  "Ychar"), keying the per-specie coefficient. Default no-op.
    virtual void calculateChemistryDriven
    (
        const volScalarField& sRhoSi,
        const word& specieName
    )
    {}

    //- Fraction of the chemistry mass change attributed to shrinkage rather
    //  than to pore opening; volPyrolysis reduces porositySource_ by the same
    //  fraction. Attribution only -- the shrinkage magnitude comes from
    //  dlambdaOverDYmi, so no volume balance is enforced between the two
    //  channels. Default 0 (all mass to porosity).
    virtual scalar chemistryMassSplit() const
    {
        return 0.0;
    }
};

} // namespace Foam

#endif
