/*---------------------------------------------------------------------------*\
    See regressionFieldValue.H for the reason this exists.
\*---------------------------------------------------------------------------*/

#include "regressionFieldValue.H"
#include "addToRunTimeSelectionTable.H"
#include "volFields.H"
#include "fvcVolumeIntegrate.H"

namespace Foam
{
namespace functionObjects
{
    defineTypeNameAndDebug(regressionFieldValue, 0);
    addToRunTimeSelectionTable(functionObject, regressionFieldValue, dictionary);
}
}

const Foam::Enum
<
    Foam::functionObjects::regressionFieldValue::operationType
>
Foam::functionObjects::regressionFieldValue::operationTypeNames_
({
    { operationType::opMin, "min" },
    { operationType::opMax, "max" },
    { operationType::opVolAverage, "volAverage" },
    { operationType::opVolIntegrate, "volIntegrate" },
});


// * * * * * * * * * * * * * * * * Constructors * * * * * * * * * * * * * * //

Foam::functionObjects::regressionFieldValue::regressionFieldValue
(
    const word& name,
    const Time& runTime,
    const dictionary& dict
)
:
    fvMeshFunctionObject(name, runTime, dict),
    writeFile(mesh_, name, "volFieldValue", dict),
    fields_(),
    operation_(opVolAverage)
{
    read(dict);
    writeFileHeader(file());
}


// * * * * * * * * * * * * * * * Member Functions * * * * * * * * * * * * * //

bool Foam::functionObjects::regressionFieldValue::read(const dictionary& dict)
{
    fvMeshFunctionObject::read(dict);
    writeFile::read(dict);

    dict.readEntry("fields", fields_);
    operation_ = operationTypeNames_.get("operation", dict);

    // Only what every regressionFunctions dict actually sets -- see the
    // class Description for why the general cases are not supported.
    const word regionType(dict.getOrDefault<word>("regionType", "all"));
    if (regionType != "all")
    {
        FatalIOErrorInFunction(dict)
            << "regressionFieldValue only supports regionType 'all', not '"
            << regionType << "'" << exit(FatalIOError);
    }

    const word weightField(dict.getOrDefault<word>("weightField", "none"));
    if (weightField != "none")
    {
        FatalIOErrorInFunction(dict)
            << "regressionFieldValue does not support weighting; "
            << "weightField must be 'none', not '" << weightField << "'"
            << exit(FatalIOError);
    }

    return true;
}


void Foam::functionObjects::regressionFieldValue::writeFileHeader(Ostream& os)
{
    if (!os.good())
    {
        return;
    }

    writeCommented(os, "Time");
    for (const word& fieldName : fields_)
    {
        os << tab << operationTypeNames_[operation_] << "(" << fieldName << ")";
    }
    os << endl;
}


bool Foam::functionObjects::regressionFieldValue::execute()
{
    return true;
}


bool Foam::functionObjects::regressionFieldValue::writeMinMax
(
    const word& fieldName
)
{
    const auto* fldPtr = mesh_.findObject<volScalarField>(fieldName);
    if (!fldPtr)
    {
        return false;
    }

    const scalar value =
        operation_ == opMin
      ? gMin(fldPtr->primitiveField())
      : gMax(fldPtr->primitiveField());

    if (Pstream::master())
    {
        file() << tab << value;
    }
    return true;
}


template<class Type>
bool Foam::functionObjects::regressionFieldValue::writeIntegral
(
    const word& fieldName
)
{
    typedef GeometricField<Type, fvPatchField, volMesh> volFieldType;

    const auto* fldPtr = mesh_.findObject<volFieldType>(fieldName);
    if (!fldPtr)
    {
        return false;
    }

    Type value = fvc::domainIntegrate(*fldPtr).value();
    if (operation_ == opVolAverage)
    {
        value /= gSum(mesh_.V());
    }

    if (Pstream::master())
    {
        file() << tab << value;
    }
    return true;
}


bool Foam::functionObjects::regressionFieldValue::write()
{
    if (Pstream::master())
    {
        writeCurrentTime(file());
    }

    for (const word& fieldName : fields_)
    {
        const bool ok =
            (operation_ == opMin || operation_ == opMax)
          ? writeMinMax(fieldName)
          : writeIntegral<scalar>(fieldName) || writeIntegral<vector>(fieldName);

        if (!ok)
        {
            WarningInFunction
                << "Requested field " << fieldName
                << " not found in database or an unsupported type; skipping"
                << endl;
        }
    }

    if (Pstream::master())
    {
        file() << endl;
    }

    return true;
}

// ************************************************************************* //
