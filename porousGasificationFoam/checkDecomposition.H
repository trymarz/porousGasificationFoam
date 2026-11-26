// Stop a parallel run whose processor*/ mesh or fields are older than the root
// case (#311): OpenFOAM reads processor*/ only, so edits made after decomposePar
// are otherwise ignored without a word. Skipped where the root files are absent.
if (Pstream::parRun())
{
    const polyBoundaryMesh& bm = mesh.boundaryMesh();
    const fileName rootDir(runTime.globalPath());
    const fileName procDir(rootDir/"processor0");
    const word timeName(runTime.timeName());

    // Non-processor patches come first and in the same order on every rank
    label nGlobal = 0;
    while (nGlobal < bm.size() && !isA<processorPolyPatch>(bm[nGlobal]))
    {
        ++nGlobal;
    }
    labelList nFaces(nGlobal);
    forAll(nFaces, patchi)
    {
        nFaces[patchi] = bm[patchi].size();
    }
    Pstream::listCombineReduce(nFaces, plusEqOp<label>());

    string problems;
    if (Pstream::master())
    {
        const fileName rootBoundary(rootDir/"constant/polyMesh/boundary");
        if (isFile(rootBoundary))
        {
            IFstream is(rootBoundary);
            token foamFile(is);
            dictionary header(is);   // the FoamFile { } block only
            PtrList<entry> patches(is);
            label nRoot = 0;
            forAll(patches, i)
            {
                const dictionary& d = patches[i].dict();
                if (d.get<word>("type") == processorPolyPatch::typeName)
                {
                    continue;
                }
                ++nRoot;
                const word& name = patches[i].keyword();
                const label rootFaces = d.get<label>("nFaces");
                const label patchi = bm.findPatchID(name);
                if (patchi < 0 || patchi >= nGlobal)
                {
                    problems += "\n    mesh: patch " + name
                        + " is not in the decomposed mesh";
                }
                else if (nFaces[patchi] != rootFaces)
                {
                    problems += "\n    mesh: patch " + name + " has "
                        + Foam::name(rootFaces) + " faces, decomposed "
                        + Foam::name(nFaces[patchi]);
                }
            }
            if (nRoot != nGlobal)
            {
                problems += "\n    mesh: " + Foam::name(nRoot)
                    + " patches, decomposed " + Foam::name(nGlobal);
            }
        }

        // Per mesh patch: the root field's entry (literal, regex or group)
        // against processor0's entry, by type
        const fileNameList fields(readDir(rootDir/timeName, fileName::FILE));
        for (const fileName& f : fields)
        {
            const word fieldName(f.has_ext("gz") ? f.lessExt() : f);
            const fileName procField(procDir/timeName/fieldName);
            if (!isFile(procField))
            {
                continue;
            }
            IFstream rootIs(rootDir/timeName/fieldName);
            IFstream procIs(procField);
            const dictionary rootDict(rootIs);
            const dictionary procDict(procIs);
            const dictionary* rootBf = rootDict.findDict("boundaryField");
            const dictionary* procBf = procDict.findDict("boundaryField");
            if (!rootBf || !procBf)
            {
                continue;
            }
            for (label patchi = 0; patchi < nGlobal; ++patchi)
            {
                const word& patch = bm[patchi].name();
                const dictionary* pd = procBf->findDict(patch, keyType::LITERAL);
                const dictionary* rd = rootBf->findDict(patch, keyType::REGEX);
                for (const word& group : bm[patchi].inGroups())
                {
                    if (!rd) rd = rootBf->findDict(group, keyType::LITERAL);
                }
                if (!pd)
                {
                    continue;
                }
                const word procType(pd->getOrDefault<word>("type", ""));
                if (!rd)
                {
                    problems += "\n    " + timeName + "/" + fieldName
                        + ": no entry for patch " + patch
                        + " (decomposed " + procType + ")";
                }
                else if (rd->getOrDefault<word>("type", "") != procType)
                {
                    problems += "\n    " + timeName + "/" + fieldName
                        + ": patch " + patch + " is "
                        + rd->getOrDefault<word>("type", "")
                        + ", decomposed " + procType;
                }
            }
        }
    }
    Pstream::broadcast(problems);

    if (!problems.empty())
    {
        FatalErrorInFunction
            << "processor*/ does not match the root case; re-run decomposePar:"
            << problems.c_str() << nl
            << exit(FatalError);
    }
}
