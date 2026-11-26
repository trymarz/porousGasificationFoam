# Apptainer images

`pgf pack` builds a self-contained Apptainer image of one PGF commit, to run cases on a server without building there.

```bash
./pgf pack                    # image of the checkout's HEAD, DEM variant
./pgf pack <build> --no-yade  # image of a build's commit, without yade
./pgf pack --dry-run          # write the build context and the definition; build nothing
```

Every option: [`pgf help pack`](pgf-reference.md#pgf-help-pack).

`pgf pack` writes `pgf-<commit12>-yade<0|1>.sif`, a self-contained image. It starts from the base image of `container/pgf-toolchain.def` (ESI's `opencfd/openfoam-run:2606`, Ubuntu 26.04 with its system gcc and Open MPI) and installs the same `openfoam2606-dev` package as that definition. Inside the image it compiles:
- the yade-pgf commit that this PGF commit pins as `submodules/yade-pgf`, headless and with the pin in its version;
- PGF itself, with `pgf build`.

Nothing from the host build is copied. The tree must be committed and clean, because the image is built from the commit, not the working tree. The pin is taken from `YADE_TRUNK` or the submodule, whichever has it. `/opt/pgf/manifest.json` records the PGF commit, build key, yade-pgf pin and coupling API hash (keys `foam_yade_pin`, `foam_yade_api_hash`), the base image, gcc and the OS.

The image is named by commit, not by build key: the key depends on the compiler, and the image's gcc differs from the host's. The image's own key is in the manifest.

On a server:

```bash
apptainer run pgf-<commit12>-yade1.sif ./Allrun     # run the case in the current directory
apptainer run pgf-<commit12>-yade1.sif status       # pgf status inside the image
```

Inside the image `pgf` reads the yade-pgf pin from the manifest, so the yade check from [Checking that the coupling is in](build.md#checking-that-the-coupling-is-in) works there as well. Building an image needs `apptainer build --fakeroot` (unprivileged user namespaces) and, for the DEM variant, a long yade compile.

## Related pages

- [Building](build.md): the build store, the DEM coupling, cleaning.
- [The pgf tool](pgf.md): settings, builds and runs.
- [Installing](install.md): a machine that builds PGF itself.
- [All documentation](README.md)
