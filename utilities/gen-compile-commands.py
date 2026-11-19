#!/usr/bin/env python3
"""Generate compile_commands.json for clangd from wmake Make/files + Make/options.

Lets clangd-based editors (nvim, VS Code, ...) do go-to-definition, find
references and diagnostics across PGF and OpenFOAM. Needs no wmake run, only
the OpenFOAM tree whose lnInclude/ directories were built (wmakeLnInclude).

Usage:
  utilities/gen-compile-commands.py [--foam DIR] [--root DIR] [--all-worktrees]
                                    [--with-yade] [--if-stale [--report]]

  --foam DIR        OpenFOAM installation (default: $WM_PROJECT_DIR)
  --root DIR        tree to index (default: the tree this script lives in)
  --all-worktrees   index every git worktree of the repository, not just --root
  --with-yade       add -DWITH_YADE=1 (index the Yade-coupled code paths)
  --if-stale        regenerate only if the source list or a Make/files|options
                    changed since the last run (stamp in .cache/), and warn
                    when lnInclude/ lacks a header; silent when all is current.
                    For hooks: OpenFOAM defaults to the one the stamp recorded.
  --report          with --if-stale: report lnInclude/ gaps even if current
  --claude-hook EV  --if-stale from a Claude Code hook (session-start,
                    post-tool-use); see .claude/settings.json

The output is gitignored and holds absolute paths, so regenerate it per machine
and per worktree (or rerun with --all-worktrees); lnInclude/ must be current.
"""
import argparse
import contextlib
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys


SKIP_DIRS = (".git", "platforms", "lnInclude", "tutorials", "submodules", ".cache", ".serena")
STAMP = os.path.join(".cache", "compile_commands.stamp")


def walk(root):
    """os.walk over the sources, skipping build output and non-indexed trees."""
    for dirpath, dirs, files in os.walk(root):
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS
                         and os.path.basename(dirpath) != "Make")
        yield dirpath, files


def fingerprint(root):
    """Hash of what the database depends on: source paths and Make/ contents."""
    h = hashlib.sha256()
    for dirpath, files in walk(root):
        for f in sorted(files):
            rel = os.path.relpath(os.path.join(dirpath, f), root)
            if f.endswith((".C", ".H")):
                h.update(rel.encode() + b"\0")
            elif os.path.basename(dirpath) == "Make" and f in ("files", "options"):
                h.update(rel.encode() + b"\0" + open(os.path.join(dirpath, f), "rb").read())
    return h.hexdigest()


def lninclude_gaps(root):
    """(lnInclude dir, missing links) wherever wmakeLnInclude is behind. Dangling links
    (deleted sources) are ignored: nothing current includes them."""
    gaps = []
    for dirpath, dirs, _ in os.walk(root):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS and d != "Make"]
        ln = os.path.join(dirpath, "lnInclude")
        if not os.path.isdir(ln):
            continue
        have = set(os.listdir(ln))
        want = {f for _, files in walk(dirpath) for f in files if f.endswith((".C", ".H"))}
        if want - have:
            gaps.append((os.path.relpath(ln, root), sorted(want - have)))
    return gaps


def pgf_foam(root):
    """WM_PROJECT_DIR as pgf's clean shell sets it (pgf's configured OpenFOAM), else None."""
    try:
        r = subprocess.run([os.path.join(root, "pgf"), "shell", "--bare", "--", "printenv",
                            "WM_PROJECT_DIR"], capture_output=True, text=True, timeout=30)
        return r.stdout.strip() or None
    except (OSError, subprocess.SubprocessError):
        return None


def worktrees(root):
    """Paths of all non-bare git worktrees of the repository containing root."""
    try:
        out = subprocess.run(["git", "-C", root, "worktree", "list", "--porcelain"],
                             capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        sys.exit("--all-worktrees needs git and a git worktree for --root")
    result, path, bare = [], None, False
    for line in out.splitlines() + [""]:
        if line.startswith("worktree "):
            path, bare = line[9:], False
        elif line == "bare":
            bare = True
        elif line == "" and path:
            if not bare and os.path.isdir(path):
                result.append(path)
            path = None
    return result


def mpi_flags():
    """Include flags for the MPI headers OpenFOAM's Pstream pulls in."""
    for cmd in ("mpicc", "mpicxx"):
        if shutil.which(cmd):
            for variant in ("--showme:incdirs", "-showme:incdirs"):
                r = subprocess.run([cmd, variant], capture_output=True, text=True)
                if r.returncode == 0 and r.stdout.strip():
                    return ["-I" + d for d in r.stdout.split()]
    return []


def api_version(foam):
    """OpenFOAM api number (e.g. 2406) from META-INFO/api-info, else None."""
    try:
        with open(os.path.join(foam, "META-INFO", "api-info")) as fh:
            m = re.search(r"api\s*=\s*(\d+)", fh.read())
            return m.group(1) if m else None
    except OSError:
        return None


def cxx_std(foam):
    """The -std flag wmake compiles with (v2406: c++14), from its Gcc rules."""
    compiler = os.environ.get("WM_COMPILER", "Gcc")
    try:
        with open(os.path.join(foam, "wmake", "rules", "General", compiler, "c++")) as fh:
            m = re.search(r"-std=\S+", fh.read())
            return m.group(0) if m else "-std=c++14"
    except OSError:
        return "-std=c++14"


def generate(root, foam, with_yade):
    subst = {
        "LIB_SRC": f"{foam}/src",
        "FOAM_SRC": f"{foam}/src",
        "FOAM_SOLVERS": f"{foam}/applications/solvers",
        "FOAM_APP": f"{foam}/applications",
        "WM_PROJECT_DIR": foam,
    }
    if os.environ.get("YADE_TRUNK"):
        subst["YADE_TRUNK"] = os.environ["YADE_TRUNK"]

    common = ["clang++", cxx_std(foam), "-m64", "-Dlinux64", "-DWM_ARCH_OPTION=64",
              "-DWM_DP", "-DWM_LABEL_SIZE=32", "-DNoRepository",
              "-ftemplate-depth-100", "-fPIC", "-Wno-invalid-offsetof"]
    api = api_version(foam)
    if api:
        common.append(f"-DOPENFOAM={api}")
    if with_yade:
        common.append("-DWITH_YADE=1")
    common += [f"-I{foam}/src/OpenFOAM/lnInclude",
               f"-I{foam}/src/OSspecific/POSIX/lnInclude"]
    common += mpi_flags() + ["-IlnInclude", "-I."]

    def expand(s):
        return re.sub(r"[$][({](\w+)[)}]", lambda m: subst.get(m.group(1), m.group(0)), s)

    def option_flags(options):
        text = open(options).read().replace("\\\n", " ")
        out = []
        for m in re.finditer(r"(-I\s*\S+|-D\S+)", text):
            f = expand(m.group(1).replace("-I ", "-I"))
            if "$(" in f or "${" in f:  # unresolved variable
                continue
            if f.startswith("-DWITH_YADE"):  # governed by --with-yade
                continue
            out.append(f)
        return out

    entries = []
    for dirpath, files in walk(root):
        if os.path.basename(dirpath) != "Make" or "files" not in files:
            continue
        comp = os.path.dirname(dirpath)
        flags = list(common)
        opt = os.path.join(dirpath, "options")
        if os.path.exists(opt):
            flags += option_flags(opt)
        text = re.sub(r"/\*.*?\*/", "", open(os.path.join(dirpath, "files")).read(), flags=re.S)
        for line in text.splitlines():
            line = line.strip()
            if line.endswith(".C") and "=" not in line and not line.startswith("//"):
                entries.append({"directory": comp, "file": os.path.join(comp, line),
                                "arguments": flags + ["-c", line]})

    out = os.path.join(root, "compile_commands.json")
    with open(out, "w") as fh:
        json.dump(entries, fh, indent=1)
    os.makedirs(os.path.join(root, ".cache"), exist_ok=True)
    with open(os.path.join(root, STAMP), "w") as fh:
        json.dump({"fingerprint": fingerprint(root), "foam": foam, "with_yade": with_yade}, fh)
    return len(entries), out


def if_stale(root, foam, with_yade, report):
    """Regenerate when the stamp is missing or old (or names another OpenFOAM than foam);
    report lnInclude/ gaps then (or if report). Never fails: it runs from hooks."""
    tag = "gen-compile-commands"
    try:
        stamp = json.load(open(os.path.join(root, STAMP)))
    except (OSError, ValueError):
        stamp = {}
    with_yade = stamp.get("with_yade", False) if with_yade is None else with_yade
    current = (os.path.exists(os.path.join(root, "compile_commands.json"))
               and stamp.get("fingerprint") == fingerprint(root)
               and (not foam or stamp.get("foam") == os.path.abspath(foam))
               and stamp.get("with_yade", False) == with_yade)
    if not current:
        foam = foam or stamp.get("foam") or pgf_foam(root)
        if foam and os.path.isdir(os.path.join(foam, "src", "OpenFOAM")):
            n, _ = generate(root, os.path.abspath(foam), with_yade)
            print(f"{tag}: sources or Make/files|options changed in {root}; regenerated "
                  f"compile_commands.json ({n} entries).")
        else:
            print(f"{tag}: WARNING compile_commands.json in {root} is STALE and OpenFOAM was not "
                  f"found to regenerate it. Run: source <OpenFOAM>/etc/bashrc && "
                  f"utilities/gen-compile-commands.py")
    if current and not report:
        return
    for ln, missing in lninclude_gaps(root):
        print(f"{tag}: WARNING {ln} has no link for {', '.join(missing[:5])}"
              + (f" (+{len(missing) - 5} more)" if len(missing) > 5 else "")
              + ". Includes of these fail in clangd/Serena until `pgf build` relinks them;"
              + " a cached build compiles nothing, then: ./pgf shell --bare -- wmakeLnInclude"
              + f" -update {os.path.dirname(ln)}")


def claude_hook(event, root, args):
    """Run --if-stale for a Claude Code hook and hand its output to Claude (JSON for
    PostToolUse, which only edits of C++ sources or Make/ files trigger)."""
    if event == "post-tool-use":
        try:
            path = json.load(sys.stdin).get("tool_input", {}).get("file_path", "")
        except ValueError:
            path = ""
        if not (path.endswith((".C", ".H")) or re.search(r"/Make/(files|options)$", path)):
            return
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        if_stale(root, args.foam, args.with_yade, report=event == "session-start")
    text = buf.getvalue().strip()
    if not text:
        return
    if event == "session-start":
        print(text)
    else:
        print(json.dumps({"systemMessage": text, "hookSpecificOutput": {
            "hookEventName": "PostToolUse", "additionalContext": text}}))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--foam", default=os.environ.get("WM_PROJECT_DIR"),
                    help="OpenFOAM installation (default: $WM_PROJECT_DIR)")
    ap.add_argument("--root", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."),
                    help="tree to index (default: the tree this script lives in)")
    ap.add_argument("--all-worktrees", action="store_true",
                    help="index every git worktree of the repository")
    ap.add_argument("--with-yade", action="store_const", const=True, default=None,
                    help="add -DWITH_YADE=1")
    ap.add_argument("--if-stale", action="store_true",
                    help="regenerate only when sources or Make/ changed; silent otherwise")
    ap.add_argument("--claude-hook", choices=("session-start", "post-tool-use"),
                    help="--if-stale for .claude/settings.json hooks (PostToolUse: JSON on stdin)")
    ap.add_argument("--report", action="store_true",
                    help="with --if-stale: report lnInclude/ gaps even when nothing changed")
    args = ap.parse_args()
    root = os.path.abspath(args.root)
    trees = worktrees(root) if args.all_worktrees else [root]

    if args.claude_hook:
        claude_hook(args.claude_hook, root, args)
        return
    if args.if_stale:
        for tree in trees:
            if_stale(tree, args.foam, args.with_yade, args.report)
        return

    if not args.foam:
        sys.exit("OpenFOAM not found: source its bashrc (sets WM_PROJECT_DIR) or pass --foam DIR")
    foam = os.path.abspath(args.foam)
    if not os.path.isdir(os.path.join(foam, "src", "OpenFOAM")):
        sys.exit(f"{foam} does not look like an OpenFOAM tree (no src/OpenFOAM)")

    for tree in trees:
        n, out = generate(tree, foam, bool(args.with_yade))
        print(f"{n} entries -> {out}")


if __name__ == "__main__":
    main()
