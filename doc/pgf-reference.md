# pgf reference

The details behind [pgf.md](pgf.md), in the same order: settings, builds, runs, then the tooling around them. `pgf help <command>` gives each command's options.

## Checks: `doctor`

`pgf doctor` runs read-only checks, each `OK`, `WARN` or `FAIL` with a fix: the bashrc and OpenFOAM version, the build of the checkout pgf runs from, yade (binary, version against the pin, `MPI` in its features), the coupling stamp, the MPI decision and the interfaces it saw, and whether `pgf` is on `PATH`. A one-line verdict ends it, and it exits 1 on any `FAIL`. The settings themselves: `pgf config`.

## Settings

### `setup` and `config`

```bash
./pgf setup --yes                          # detect OpenFOAM and yade, write the user file, link ~/.local/bin/pgf, summarise the choices
./pgf setup --checkout --yes --no-link --foam-bashrc FILE   # write the checkout's .pgf.conf instead, no link
./pgf setup --link-only                    # only ~/.local/bin/pgf -> the pgf of the checkout it runs in
./pgf config get PGF_MPI                   # one resolved value (exit 1 if unset)
./pgf config set PGF_MPI=loopback --checkout   # edit the user (default) or checkout file
./pgf config unset PGF_MPI --checkout
```

- `setup` looks for `$WM_PROJECT_DIR/etc/bashrc`, `~/OpenFOAM/*/etc/bashrc`, `/opt/openfoam*` and `/usr/lib/openfoam/*` (v2406 first), and for `yade` or a suffixed `yade-*` on your `PATH`. It asks when several candidates exist and a terminal is attached, unless `--yes`. Values given as `--foam-bashrc`, `--yade-bin`, `--yade-trunk` or `--set` win, and keys already in the file are kept. `--checkout` also records `submodules/Foam-Yade` as `YADE_TRUNK` when it is checked out.
- The link step refuses to replace a link to another checkout without `--force`. With several worktrees, the link picks one; call the others by path.
- `setup` (not `--link-only`) also offers to install [bash completion](#bash-completion): it asks on a terminal, installs with `--yes`, and skips with `--no-completion`.

### Keys and layers

The keys are `PGF_FOAM_BASHRC`, `YADE_TRUNK`, `YADE_BIN`, `YADE_PYTHONPATH`, `PGF_YADE_LIBDIR`, `PGF_BUILD_ROOT`, `PGF_RUN_ROOT`, `PGF_CASE_ROOT`, `PGF_YADE=0|1`, `PGF_MPI=default|loopback|auto` and `PGF_BUILD` (see [Choosing a build](#choosing-a-build); not in the user file). Each comes from layers; a later layer wins:

1. built-in default;
2. the user file, `${XDG_CONFIG_HOME:-~/.config}/pgf/env.conf`;
3. the checkout file, `.pgf.conf` at the tree root. The old name `.pgf-env.conf` is still read, with a note, while no `.pgf.conf` exists; the next `pgf config set --checkout` or `pgf setup --checkout` writes its keys to `.pgf.conf`;
4. the caller's environment;
5. `--set KEY=VALUE` (or `--foam-bashrc FILE`, `--yade` / `--no-yade`, `--mpi …`).

The files hold `KEY=value` lines and `#` comments. They are parsed, not sourced: an unknown key or a malformed line exits 2 naming `file:line`, and only the keys above are accepted, so a file cannot set `PATH` or run code. `PGF_YADE=1` makes `--yade` the default; `PGF_MPI=loopback` does what `--mpi loopback` does. `pgf status` and `pgf config` print every effective value and the layer (and `file:line`) it came from; `pgf config --json` gives the same as JSON.

## Builds

What a build is and how its key is made: [build.md → The build store](build.md#the-build-store).

### Choosing a build

Each command uses one build. It comes from, later winning: **auto** (the build of the sources as they are now in the checkout pgf runs from), `PGF_BUILD` in the checkout's `.pgf.conf`, the terminal (`PGF_BUILD` in the environment, which `pgf use` sets), and `--use SEL` on the command line. `PGF_BUILD` is refused in the user file. `SEL` is a key or unique key prefix, a commit (the newest build made from it), a worktree name or path (its last build), a prefix path, or `auto`. `--prefix DIR` uses that complete prefix as it is (also one built with `build.sh --prefix` outside the store) and skips the staleness check.

`pgf use SEL` prints the line that sets the terminal layer; apply it with `eval "$(pgf use SEL)"`. Inside `pgf shell` a `pgf` function does the `eval`; the shell itself keeps the build it started with, so later `pgf shell` / `pgf run` calls follow the new choice.

A selected build that is not of that checkout's current sources is **stale**: `pgf shell` warns, `pgf run` refuses. `pgf run --build` builds that checkout first (it cannot be combined with a selected build), and `pgf run --allow-stale` runs a stale build anyway. The banner names the layer the build came from.

### Listing and removing builds

`pgf status` lists the two keys of the checkout pgf runs from (`yade0`, `yade1`) and whether each is built; `pgf status --json` adds every setting and its layer, and `pgf status PATH` asks another checkout.

`pgf builds [--json]` lists the registered workspaces, then every build: its variant, age, size, the checkout that built it, the checkouts whose last build it is, `[in use]` and `[building]`; `*` marks those of the checkout pgf runs from. `pgf ls` is its old name.

Every `pgf shell` and `pgf run` holds a shared `flock` on `$PGF_BUILD_ROOT/<key>.use.lock` while it runs. `pgf gc [--keep N] [--yes] [--dry-run] [--json]` removes builds that are not the current ones of the checkout pgf runs from, not a registered workspace's last build, not in use and not being built, keeping the `N` newest of the rest (default 2). Without `--yes` it asks on a terminal and refuses (exit 2) without one. `pgf test --gc` also skips builds in use.

## Runs

### Copying a tutorial: `copy`

```bash
pgf copy --list                              # the tutorials, one line each
pgf copy charOnlyMoveCases/serial            # to $PGF_CASE_ROOT/<branch>/charOnlyMoveCases/serial
pgf copy canonical/pyrolysis pyrolysis-2     # a second copy under another name
```

`copy` copies `tutorials/cases/<tutorial>` whole, with linked files resolved. `<branch>` is the branch of the checkout pgf runs from with `/` as `-`, or its short commit when detached. The copy holds `.pgf-source.json`, the checkout and commit it came from: `pgf run` then uses that checkout's build, and warns when the branch has moved on since. An existing copy is kept unless `--force`. Run such copies, not the tutorials in the checkout.

### Running a case: `run`

`run` changes into the case directory (resolved from your current directory) and runs the script, `Allrun` by default, in the clean environment of [`shell`](#the-clean-environment-shell). It works from anywhere. A case whose `constant/yadeProperties` says `active true` gets `--yade` unless `--no-yade` is given or `PGF_YADE` is set in a file or the environment.

### The run record

After the script ends, `run` writes a run record to `<case>/.pgf-run.json`: the command, start and end times, exit status, the build (key, prefix, variant, commit, and the layer it was selected by) and, for a DEM run, the yade binary, its commit and the Foam-Yade pin. The same record is appended as one line to `${XDG_STATE_HOME:-~/.local/state}/pgf/runs/runs.jsonl` when that is writable (before #274 this log lived under the build root). `run` exits with the script's status, and still writes the record after Ctrl-C.

### The clean environment: `shell`

```bash
pgf shell                                # interactive bash, prompt (pgf:<worktree>@<key>)
pgf shell -c 'command -v porousGasificationFoam'
pgf shell -- applications/test/regression/Allrun
```

`pgf shell [--prefix DIR | --for-tree | --bare] [--yade] [--mpi loopback] -- <command>` (old name `pgf exec`) runs a command in a clean environment. It drops the caller's environment except `HOME USER LANG TERM TMPDIR WM_PROJECT_DIR XDG_CONFIG_HOME XDG_STATE_HOME PGF_* YADE_*` and sources `$PGF_FOAM_BASHRC` (default `$WM_PROJECT_DIR/etc/bashrc`) with `set +eu`. Then it points `WM_PROJECT_USER_DIR`, `FOAM_USER_APPBIN` and `FOAM_USER_LIBBIN` at the build and removes every `PATH` / `LD_LIBRARY_PATH` entry under the user dir `bashrc` chose. It also sets `UCX_VFS_ENABLE=n UCX_LOG_LEVEL=fatal`, `PGF_ISOLATED_ONLY=1` (`build.sh` then refuses to build without `--prefix` / `--isolated`) and `TMPDIR=$PGF_RUN_ROOT/tmp`. It checks that `porousGasificationFoam` resolves into the build and prints on stderr what it uses: OpenFOAM, the build and what chose it, and MPI when it restricts it.

- `--for-tree` (default) uses the build of the checkout pgf runs from and exits 2 if it is not built. `--bare` uses no build; use it to run `build.sh`.
- `--yade` adds `YADE_TRUNK` (default `submodules/Foam-Yade`), a `yade` on `PATH` (from `YADE_BIN` or the caller's `PATH`) and `PYTHONPATH` (`YADE_PYTHONPATH`, or derived from the install). If `/tmp` is read-only, it also preloads a small shim that moves Yade's `/tmp/yade-XXXXXX` into `$TMPDIR`.
- `--mpi loopback` restricts Open MPI to `self,vader,tcp` on `lo`. The DEM coupling connects Yade's MPI job to the solver it spawns over TCP, and Open MPI leaves `lo` out by default, so on a host or sandbox whose only interface is `lo` the run hangs with `help-mca-bml-r2.txt / unreachable proc`; on a host with docker/libvirt bridges it can hang probing them. The default, `PGF_MPI=auto`, applies loopback when a `docker*`, `virbr*`, `br-*` or `veth*` interface is up or when not exactly one other interface is up, and says so in the banner (`MPI loopback (auto: …)`). `--mpi default` (or `PGF_MPI=default`) leaves Open MPI alone. Runs are single-host; `auto` does not handle multi-host jobs.
- Nested `pgf` calls inside it skip the `env -i` step.

## Bash completion

`pgf completion bash` prints the script; `pgf setup` installs it to `${BASH_COMPLETION_USER_DIR:-${XDG_DATA_HOME:-~/.local/share}/bash-completion}/completions/pgf`, where the bash-completion package loads it on the first Tab. Without that package, source the file from `~/.bashrc`. Rerun `pgf setup` (or `pgf completion bash > FILE`) after updating pgf.

The script is generated from the command table in `utilities/pgf/cli.sh`, the same table `pgf help` prints, so the two cannot drift. It completes every subcommand and its options. It also completes these values, asking `pgf __complete` at Tab time:

- build keys, `auto` and worktree names after `use`, `pack` and `--use`;
- `get`/`set`/`unset`, then keys after `config` (and `KEY=` then values for `config set` and `--set`);
- `auto`/`loopback`/`default` for `--mpi` and `PGF_MPI=`, `0`/`1` for `PGF_YADE=`;
- directories after `run`, then that case's scripts;
- tiers from `applications/test/tiers.conf` (and `all`) after `test`;
- `build.sh` options after `build`.

Build keys need the build root. When `PGF_BUILD_ROOT` is not set, finding it sources OpenFOAM, which takes about a second; set `PGF_BUILD_ROOT` to make that Tab instant.

## Scripts at the top of the checkout

**`pgf`**: settings, builds, runs in a clean environment, test tiers.
- *Why:* one front door instead of hand-sourcing OpenFOAM and sharing `$FOAM_USER_APPBIN`.
- *Instead:* `./Allwmake` / `./Allwclean` if you only want the OpenFOAM-style entry point.

**`Allwmake`**: runs `pgf build --all --no-DEM [args]`.
- *Why:* `./Allwmake` is where OpenFOAM users look first.
- *Instead:* `pgf build [--yade]` for options; `utilities/pgf/build.sh` for an in-place build.

**`Allwclean`**: runs `pgf build clean --all [args]`, DEM included.
- *Why:* the same convention; cleaning DEM too means no `--yade` state survives a switch back.
- *Instead:* `pgf build clean [targets]` for one target; add `--purge` to drop the installed build.

**`build.sh`**: deprecated shim (#274), forwards to `utilities/pgf/build.sh` so old scripts keep working.
- *Instead:* `pgf build`, or `utilities/pgf/build.sh` in place.

**`pgf-test`**: deprecated shim (#274), forwards to `pgf test` so old scripts keep working.
- *Instead:* `pgf test <tier|all>`, `--status`, `--gc`.

Old names (#274, #287): `pgf-env` is gone (use `pgf shell`, `pgf setup`, …); `pgf-test` is now `pgf test`; `pgf where`, `pgf ls` and `pgf exec` still work as `pgf status`, `pgf builds` and `pgf shell --`; the build driver moved from `build.sh` to `utilities/pgf/build.sh`. The root `pgf-test` and `build.sh` still forward to the new commands, with a note on stderr, for a deprecation period.

Tests for pgf itself live in `applications/test/tooling/test_*.sh`; each runs alone with `PGF_FOAM_BASHRC` set and builds under `PGF_BUILD_ROOT` (default `$TMPDIR/pgf-tooling-builds`).

## Related pages

- [The pgf tool](pgf.md): settings, builds and runs.
- [Building](build.md): the build store, the DEM coupling, cleaning.
- [All documentation](README.md)
