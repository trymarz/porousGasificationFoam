# Building porousGasificationFoam

## Build System

### Where things are built and where they land

porousGasificationFoam uses the ordinary OpenFOAM user-build layout. `wmake`
runs **in place**, in the component directories of your checkout — the checkout
may live anywhere, and nothing is copied into `$WM_PROJECT_USER_DIR`.

| What | Where |
|---|---|
| Source | your checkout, wherever you cloned it |
| `lnInclude/`, `Make/<WM_OPTIONS>/` | beside the source, inside the checkout (git-ignored) |
| Libraries | `$FOAM_USER_LIBBIN` |
| Executables | `$FOAM_USER_APPBIN` |

`$FOAM_USER_LIBBIN` is already on the runtime library path once you have sourced
OpenFOAM's `etc/bashrc`, so no extra environment file has to be sourced before
building or running. (Earlier releases shipped a `porousGasificationMediaDirectories`
helper that mirrored the sources under `$WM_PROJECT_USER_DIR/applications` and
exported `FOAM_HGS`. Both the file and the mirror are gone; if you still have an
old mirror lying around, delete it — nothing consults it any more, and it can
only confuse a header search.)

`build.sh` resolves its own location, so `./Allwmake`, `./build.sh …` and
`/abs/path/to/checkout/Allwmake` all behave identically from any working
directory.

### Generated artifacts and cleaning

A build writes `lnInclude/` and `Make/<WM_OPTIONS>/` next to each component's
sources. Both are git-ignored; neither should ever be committed.

`./Allwclean` (or `./build.sh clean …`) removes that generated state through
`wclean`. Like `wclean`, it leaves the installed libraries and executables in
place: `$FOAM_USER_LIBBIN` and `$FOAM_USER_APPBIN` are shared by every project
built into this `$WM_PROJECT_USER_DIR`, so a clean in one checkout has no
business reaching into them.

When you do want a guaranteed blank slate — before switching build variants, or
after a rebuild failed and you would rather have no solver than the previous
one — add `--purge`:

```bash
./build.sh clean --all --purge
```

That additionally deletes the libraries and executables the cleaned targets
declare in their own `Make/files`, and nothing else. Other libraries in
`$FOAM_USER_LIBBIN`, including Foam-Yade's, are left alone.

One thing `wmake` will not do on its own is prune a symlink in `lnInclude/`
whose source file you deleted or renamed; it is left dangling, so an include of
it fails loudly rather than resolving to stale content. Run `./Allwclean` after
renaming or deleting headers to clear it.

### Build targets

| Target | Type | Dependency |
|---|---|---|
| DEM (`--yade` only) | library | Foam-Yade coupling libs |
| fieldPorosityModel | library | — |
| radiationModels | library | solid thermo |
| thermophysicalModels | library suite | — |
| pyrolysisModels | library | all above |
| porousGasificationFoam | executable | all libraries |
| utilities | executable suite | — |

### Fine-grained control with build.sh

The `build.sh` script operates in two modes: `build` or `clean`. For the selected mode it builds or cleans all active targets. Targets are switched on or off via CLI flags.

Examples:

```bash
./build.sh build --apps-only # set app targets to 1 and libs to 0, build targets with 1 set
./build.sh clean --reset-all --radiationModels # set all targets to 0; set radiationModels to 1, clean only radiationModels (later flags overwrite previous ones)
./build.sh build --all --no-DEM # skip DEM, build all the rest

# Aliases
./Allwmake  → ./build.sh build --all --no-DEM   # normal build: no DEM/Yade
./Allwclean → ./build.sh clean --all            # cleans DEM too
```

Add `--purge` to a clean to also delete the installed libraries and
executables; without it, cleaning matches `wclean` and touches build state only.

Both wrappers forward any extra arguments, so `./Allwmake --yade` is the
DEM-enabled build.

Run `build.sh --help` for all options.

### YADE DEM coupling

```bash
./build.sh build --yade
```

`--yade` enables the `DEM` library target and compiles the solver with `WITH_YADE=1`, which activates the `#ifdef WITH_YADE` DEM coupling blocks. Without this flag the solver is built without DEM support and Yade will hang waiting for MPI communication.

Requires YADE installed with the OpenFOAM coupling module, with `YADE_TRUNK`
pointing at the Foam-Yade source checkout. `build.sh` checks both before it
compiles anything, so a missing prerequisite is reported as an error instead of
a linker failure — and an existing normal build is left untouched.

**Switching between the normal and `--yade` builds requires a clean.** `wmake`
decides what to recompile from source timestamps; it does not know that
`WITH_YADE` changed, so an in-place rebuild after a mode switch would relink a
mix of the two variants. `build.sh` records which variant produced the objects
currently in the tree and refuses to build the other one on top of them:

```bash
./build.sh clean --all --purge   # then
./build.sh build --yade          # (or ./Allwmake for the normal build)
```

`--purge` is the right choice here specifically: it drops the installed solver
too, so a rebuild that fails partway cannot leave the *other* variant's binary
on your `PATH`.

Only one variant can exist in a given `$FOAM_USER_APPBIN` at a time; the two are
not built side by side.

### Doxygen documentation

```bash
cd doc/Doxygen
./Allwmake
```

Output: `$WM_PROJECT_DIR/doc/Doxygen/html/index.html`. Requires `doxygen` and `graphviz`.

The wrapper derives the source root from its own location, so it may also be run
by absolute path from anywhere; set `POROUS_DOC_SRC` explicitly only if you want
to document a different tree.
