# Developer tools

Tools for working on PGF's source: editor navigation, the agent's code index and the API reference. None of them is needed to build or run PGF.

## Editor navigation (clangd)

For go-to-definition, find-references and diagnostics in any clangd-based editor (nvim, VS Code, ...), generate a compilation database from the `Make/files` and `Make/options` of each component:

```bash
source <OpenFOAM>/etc/bashrc        # sets WM_PROJECT_DIR; or pass --foam DIR
utilities/gen-compile-commands.py                   # the worktree you run it in
utilities/gen-compile-commands.py --all-worktrees   # every git worktree
```

It writes a gitignored `compile_commands.json` at each tree's root, so rerun it per machine and per worktree. It reads the `lnInclude/` directories, so build (or run `wmakeLnInclude`) first. The C++ standard comes from OpenFOAM's `wmake/rules`, as in a real build. `--with-yade` also indexes the `WITH_YADE` code paths; without it that code is greyed out.

Rerun it after adding, removing or renaming a `.C` file or a header, or after editing a `Make/files` or `Make/options`. clangd picks up edits to existing files by itself, but not a changed list of files.

`utilities/gen-compile-commands.py --if-stale` does that only when needed: it regenerates the database (in well under a second) when the list of sources or a `Make/files`/`Make/options` changed since its last run, warns when an `lnInclude/` lacks a link for a new or renamed file, and is silent otherwise. Claude Code runs it at session start and after each edit of a `.C`, `.H` or `Make/` file (`.claude/settings.json`). Git runs it after checkout, merge and rebase once the repository's [git hooks](#git-hooks) are enabled.

`pgf build` relinks `lnInclude/`. A cached build compiles nothing and leaves it alone; then `./pgf shell --bare -- wmakeLnInclude -update <component dir>` adds the missing links.

## Agent navigation (Serena)

Claude Code sessions in this repository also start [Serena](https://github.com/oraios/serena) (`.mcp.json`, `.serena/project.yml`), which gives the agent symbol lookups over the same database. One command sets it up; rerun it in any worktree that was never built or indexed:

```bash
utilities/serena-setup                                  # this worktree
utilities/serena-setup --all                            # every worktree with Serena config
utilities/serena-setup --with foamcli --with yade-pgf   # also these sibling repositories
utilities/serena-setup --with-submodules                # also submodules/yade-pgf, if checked out
```

It installs Serena with `uv tool install` into `~/.local` when missing, enables the [git hooks](#git-hooks), links `lnInclude/` without compiling, writes `compile_commands.json` and indexes the worktree (minutes on a first run). A new worktree's index starts from the newest one among the other worktrees when their files share the same compile flags, so only the files that differ are indexed (seconds instead of minutes). `--fresh` rebuilds an index from scratch.

Sibling repositories are set up only when named with `--with`, or given by path. They get a local Serena config (`.serena/`, `.mcp.json`, kept out of git through `.git/info/exclude`) and an index. A Python repository such as foamcli needs nothing else. A CMake repository such as yade-pgf gets a configure-only CMake run (no compile, about 10 s) in `~/.cache/serena-setup/`; its compile database goes to the repository's root without the GCC-only flags clangd rejects. YADE is configured as in the container recipe (`-DENABLE_GUI=OFF`); `SERENA_CMAKE_ARGS` adds options. These repositories have no freshness hooks, so rerun the command after adding or removing sources there.

`--with-submodules` treats each PGF worktree's checked-out `submodules/yade-pgf` the same way, as a project of its own (its excludes go to the submodule's git directory); a submodule that is not checked out is skipped with a note. The worktree's own index still ignores `submodules/`, so a session started in the worktree does not see YADE symbols; start one in the submodule for that.

Serena runs locally and calls no AI service; it hands clangd's answers to the agent. Its first start downloads clangd into `~/.serena`. `.mcp.json` turns off its anonymous usage ping and keeps its web dashboard from opening a browser; the dashboard (logs and tool calls) stays reachable on 127.0.0.1 and checks for Serena updates.

`.mcp.json` finds `serena` on `PATH` or in `~/.local/bin`. Claude Code asks once per project to approve the server. A session opened at the bare repository's root, outside every worktree, has no `.mcp.json` and so no Serena. Every worktree's project is named `porousGasificationFoam`; Serena finds it by path, so worktrees can be open side by side.

## Git hooks

The repository's hooks are in `utilities/githooks/`. Enable them once per clone; this covers all its worktrees:

```bash
git config core.hooksPath utilities/githooks
```

- `post-checkout`, `post-merge`, `post-rewrite`: run `utilities/gen-compile-commands.py --if-stale`, so `compile_commands.json` follows checkouts, merges and rebases ([clangd](#editor-navigation-clangd)).
- `pre-push`: refuses a push to the public repository when an outgoing commit touches a path in `utilities/githooks/private-paths` ([Contributing → Private companion repository](../CONTRIBUTING.md#private-companion-repository)).

## API reference (Doxygen)

```bash
cd doc/Doxygen
./Allwmake
```

Output: `$WM_PROJECT_DIR/doc/Doxygen/html/index.html`. Needs `doxygen` and `graphviz`. The wrapper finds the source root from its own location, so it can be run by absolute path from anywhere; set `POROUS_DOC_SRC` only to document a different tree.

## Related pages

- [The build system](build.md): `pgf build`, and the `lnInclude/` the tools above read.
- [Contributing](../CONTRIBUTING.md): branches, PRs and where documentation goes.
- [All documentation](README.md)
