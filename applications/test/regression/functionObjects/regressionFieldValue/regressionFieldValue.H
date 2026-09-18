/*---------------------------------------------------------------------------*\
Class
    Foam::functionObjects::regressionFieldValue

Description
    Minimal, whole-domain scalar-field reduction (min/max/volAverage/
    volIntegrate) for the PGF regression suite's baseline capture.

    Exists only to avoid dlopen-ing stock OpenFOAM's libfieldFunctionObjects,
    whose volFieldValue would otherwise serve this. That library also links
    libreactingMultiphaseSystem (for unrelated multiphase function objects
    bundled into the same .so), and that library's static-initializer object
    gets double-freed at process exit against one of PGF's own libraries'
    __cxa_finalize call -- see issue #76 / plan
    fix-regression-suite-teardown-abort-2026-08-04 for the full trace. PGF's
    own physics never uses multiphase flow, so this function object supports
    only the subset every regressionFunctions dict actually needs:
    regionType "all", weightField "none", and the four listed operations.

    Writes to postProcessing/<name>/<time>/volFieldValue.dat (same relative
    path and column layout stock volFieldValue used) so every already
    -committed reference/ baseline applies unchanged.

SourceFiles
    regressionFieldValue.C

\*---------------------------------------------------------------------------*/

#ifndef Foam_functionObjects_regressionFieldValue_H
#define Foam_functionObjects_regressionFieldValue_H

#include "fvMeshFunctionObject.H"
#include "writeFile.H"
#include "Enum.H"

namespace Foam
{
namespace functionObjects
{

/*---------------------------------------------------------------------------*\
                   Class regressionFieldValue Declaration
\*---------------------------------------------------------------------------*/

class regressionFieldValue
:
    public fvMeshFunctionObject,
    public writeFile
{
public:

    enum operationType
    {
        opMin,
        opMax,
        opVolAverage,
        opVolIntegrate
    };

    static const Enum<operationType> operationTypeNames_;


private:

    // Private Data

        wordList fields_;
        operationType operation_;


    // Private Member Functions

        void writeFileHeader(Ostream& os);

        //- min/max: scalar fields only (no dict needs vector extrema, and
        //- there is no natural per-cell ordering to reduce over).
        bool writeMinMax(const word& fieldName);

        //- volAverage/volIntegrate: any GeometricField<Type> via
        //- fvc::domainIntegrate, so a vector field like U works too.
        template<class Type>
        bool writeIntegral(const word& fieldName);


public:

    //- Runtime type information
    TypeName("regressionFieldValue");


    // Constructors

        regressionFieldValue
        (
            const word& name,
            const Time& runTime,
            const dictionary& dict
        );

        //- No copy construct
        regressionFieldValue(const regressionFieldValue&) = delete;

        //- No copy assignment
        void operator=(const regressionFieldValue&) = delete;


    //- Destructor
    virtual ~regressionFieldValue() = default;


    // Member Functions

        virtual bool read(const dictionary& dict);
        virtual bool execute();
        virtual bool write();
};

} // End namespace functionObjects
} // End namespace Foam

#endif

// ************************************************************************* //
