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
// driven in solveSpeciesMass(): beginStep() once per step, then
// calculateTemperatureDriven() once, then calculateChemistryDriven() per
// solid specie, then endStep() once. PGF owns only what it sends YADE: the
// rate [m/s] (YADE integrates State::lambda_), or the factor under
// apparentVolume, applied once per exchange.
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

    //- Zeroes lambdaDot_. Call once per step, before
    //  calculateTemperatureDriven() and calculateChemistryDriven(), which
    //  both add onto it with +=. Not virtual: the reset is the same for
    //  every model, so no calculate*Driven() override needs to double as it.
    void beginStep();

    //- Adds the temperature-driven contribution to lambdaDot_. Called once
    //  per step, before the species loop.
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

    //- As above, also given this specie's Ym_i [kg/m^3], not yet solved this
    //  step. Default forwards to the two-argument form, so existing models
    //  need no change.
    virtual void calculateChemistryDriven
    (
        const volScalarField& sRhoSi,
        const volScalarField& Ym_i,
        const word& specieName
    )
    {
        calculateChemistryDriven(sRhoSi, specieName);
    }

    //- Called once after the species loop. Default no-op.
    virtual void endStep()
    {}

    //- Called once after construction with each solidComponents name and its
    //  apparent specific volume lam3 = 1/rhoApparent [m^3/kg]. Default no-op.
    virtual void setApparentSpecificVolumes
    (
        const wordList& specieNames,
        const scalarList& lam3
    )
    {}
};

} // namespace Foam

#endif
