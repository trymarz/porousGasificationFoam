# Clean rebuild and uninstall

This page undoes what [Installing](install.md) and `pgf` set up: first a clean rebuild that keeps PGF, then removing PGF, YADE and OpenFOAM. `pgf status` prints where your settings, builds, run records, scratch files and case copies are; the paths below are the defaults.

## Clean rebuild

PGF's in-tree build cache (`lnInclude/` and `Make/<WM_OPTIONS>/` in each library and the solver) remembers the OpenFOAM tree it was built with. After switching to another OpenFOAM, or installing it elsewhere, the next build can stop with `No rule to make target …/lnInclude/…` ([#360](https://github.com/trymarz/porousGasificationFoam/issues/360)). Clean the cache, point pgf at the OpenFOAM you use now, and build again:

```bash
./Allwclean                 # same as pgf build clean --all, DEM targets included
./pgf setup --foam-bashrc <OpenFOAM>/etc/bashrc
pgf build                   # add --yade for the DEM variant
```

`./Allwclean` leaves the built prefixes in the build store alone; `pgf gc` removes the ones no checkout uses ([`pgf help gc`](pgf-reference.md#pgf-help-gc)).

## Removing PGF

`pgf` keeps nothing outside these places:

| What | Default location | Made by |
|---|---|---|
| The `pgf` link on your `PATH` | `~/.local/bin/pgf` | `pgf setup` |
| Bash completion | `~/.local/share/bash-completion/completions/pgf` | `pgf setup` |
| User settings | `~/.config/pgf/` (`env.conf`) | `pgf setup`, `pgf config` |
| Checkout settings | `<checkout>/.pgf.conf` | `pgf setup --checkout` |
| Run records and last test outcomes | `~/.local/state/pgf/` | `pgf run`, `pgf test` |
| Cached yade commit lookups | `~/.cache/pgf/` | `pgf doctor`, `pgf test dem` |
| Builds | `$WM_PROJECT_USER_DIR/pgf-builds` (`PGF_BUILD_ROOT`) | `pgf build` |
| Test runs and scratch | `$TMPDIR/pgf-runs` (`PGF_RUN_ROOT`) | `pgf test` |
| Case copies | `$WM_PROJECT_USER_DIR/run/<branch>/` (`PGF_CASE_ROOT`) | `pgf copy` |

`$WM_PROJECT_USER_DIR` is OpenFOAM's per-user directory, for v2606 usually `~/OpenFOAM/$USER-v2606`. The `XDG_*_HOME` variables move the `~/.config`, `~/.local/state`, `~/.local/share` and `~/.cache` entries.

To remove PGF, delete what you no longer need from the table, then the checkout itself. Keep the case copies if they hold results you want. For the default locations:

```bash
rm -f ~/.local/bin/pgf ~/.local/share/bash-completion/completions/pgf
rm -rf ~/.config/pgf ~/.local/state/pgf ~/.cache/pgf
rm -rf "${TMPDIR:-/tmp}/pgf-runs"
rm -rf ~/OpenFOAM/$USER-v2606/pgf-builds
rm -rf <checkout>           # the git clone, with its submodules
```

## Removing YADE (DEM coupling)

The YADE build of [DEM coupling](install.md#3-dem-coupling-optional) lives in two directories, the install prefix and the build tree:

```bash
rm -rf ~/opt/yade-pgf ~/build/yade-pgf
```

The system packages it needed stay; remove them with `apt` if nothing else uses them. If you swapped VTK packages for the build ([ParaView and Yade's VTK](install.md#paraview-and-yades-vtk)), `sudo apt-mark auto libvtk9.3` lets `apt autoremove` take the runtime library again.

## Removing OpenFOAM

Only if nothing else uses it. ESI's package:

```bash
sudo apt-get remove openfoam2606-dev
```

An OpenFOAM built from source is its own directory tree (`OpenFOAM-v2606`, `ThirdParty-v2606`). `~/OpenFOAM/$USER-v2606` holds your OpenFOAM user files and PGF's builds and case copies; remove it only when none of them is needed.

## Related pages

- [Installing](install.md)
- [The pgf tool → Settings](pgf.md#settings), [`pgf help config`](pgf-reference.md#pgf-help-config)
- [All documentation](README.md)
