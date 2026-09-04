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
// then calculateChemistryDriven() per solid specie. PGF owns only this rate
// -- YADE integrates lambda itself (State::lambda_); no model here reads it.
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

    //- Temperature-driven part — called once per step before the species
    //  loop. Sets (overwrites) lambdaDot_ with the current temperature-driven
    //  contribution.
    virtual void calculateTemperatureDriven() = 0;

    //- Chemistry-driven part: adds this specie's mass-rate sRhoSi
    //  [kg/m^3/s] contribution to lambdaDot_. specieName is the bare
    //  solidComponents name ("char", not "Ychar"), for per-specie
    //  coefficient lookup. Default empty; only exactDifferentialLambdaDot
    //  overrides it.
    virtual void calculateChemistryDriven
    (
        const volScalarField& sRhoSi,
        const word& specieName
    )
    {}

    //- Fraction of the chemistry mass change redirected to lambda (the rest
    //  becomes pore space); volPyrolysis reduces porositySource_ by this
    //  same fraction, keeping the split conservative. Default 0 (all mass
    //  to porosity); only exactDifferentialLambdaDot overrides it.
    virtual scalar chemistryMassSplit() const
    {
        return 0.0;
    }
};

} // namespace Foam

#endif
