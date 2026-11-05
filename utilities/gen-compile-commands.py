#!/usr/bin/env python3
"""Generate compile_commands.json for clangd from wmake Make/files + Make/options.

Lets clangd-based editors (nvim, VS Code, ...) do go-to-definition, find
references and diagnostics across PGF and OpenFOAM. Needs no wmake run, only
the OpenFOAM tree whose lnInclude/ directories were built (wmakeLnInclude).

Usage:
  utilities/gen-compile-commands.py [--foam DIR] [--root DIR] [--all-worktrees]
                                    [--with-yade]

  --foam DIR        OpenFOAM installation (default: $WM_PROJECT_DIR)
  --root DIR        tree to index (default: the tree this script lives in)
  --all-worktrees   index every git worktree of the repository, not just --root
  --with-yade       add -DWITH_YADE=1 (index the Yade-coupled code paths)

The output is gitignored and holds absolute paths, so regenerate it per machine
and per worktree (or rerun with --all-worktrees); lnInclude/ must be current.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys


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

    common = ["clang++", "-std=c++17", "-m64", "-Dlinux64", "-DWM_ARCH_OPTION=64",
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
    for dirpath, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in (".git", "platforms", "lnInclude", "tutorials", "submodules")]
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
    print(f"{len(entries)} entries -> {out}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--foam", default=os.environ.get("WM_PROJECT_DIR"),
                    help="OpenFOAM installation (default: $WM_PROJECT_DIR)")
    ap.add_argument("--root", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."),
                    help="tree to index (default: the tree this script lives in)")
    ap.add_argument("--all-worktrees", action="store_true",
                    help="index every git worktree of the repository")
    ap.add_argument("--with-yade", action="store_true", help="add -DWITH_YADE=1")
    args = ap.parse_args()

    if not args.foam:
        sys.exit("OpenFOAM not found: source its bashrc (sets WM_PROJECT_DIR) or pass --foam DIR")
    foam = os.path.abspath(args.foam)
    if not os.path.isdir(os.path.join(foam, "src", "OpenFOAM")):
        sys.exit(f"{foam} does not look like an OpenFOAM tree (no src/OpenFOAM)")

    root = os.path.abspath(args.root)
    for tree in worktrees(root) if args.all_worktrees else [root]:
        generate(tree, foam, args.with_yade)


if __name__ == "__main__":
    main()
