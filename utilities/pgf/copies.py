#!/usr/bin/env python3
"""copies.py -- pgf copy's records (#287): which checkout and commit a case copy
came from, and whether that checkout has moved on since.

  copies.py tutorials CHECKOUT       tutorials, one "path<TAB>what" line each
  copies.py record COPY CHECKOUT TUTORIAL
                                     write COPY/.pgf-source.json
  copies.py check CASE               "checkout<TAB>INFO line<TAB>WARN line"
                                     for a recorded copy; nothing otherwise
  copies.py list ROOT                the copies under ROOT, per branch dir

A copy is stale when its checkout is gone, is on another branch, or its HEAD
is no longer the recorded commit. Paths print with $HOME as ~.
"""

import datetime
import fnmatch
import json
import os
import re
import subprocess
import sys

RECORD = ".pgf-source.json"


def tilde(p):
    home = os.environ.get("HOME", "")
    if home and (p == home or p.startswith(home + "/")):
        return "~" + p[len(home):]
    return p


def git(checkout, *args):
    try:
        out = subprocess.run(["git", "-C", checkout, *args], capture_output=True,
                             text=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired):
        return ""
    return out.stdout.strip() if out.returncode == 0 else ""


def tutorials(checkout):
    """(path, what) per case under tutorials/cases: a dir with an executable
    Allrun and system/ (or a family: children with system/ or constant/);
    `what` from doc/case-setup.md's Tutorial Cases."""
    base = os.path.join(checkout, "tutorials", "cases")
    found = []
    for d, dirs, files in os.walk(base):
        dirs.sort()
        isCase = os.path.isdir(os.path.join(d, "system")) or any(
            os.path.isdir(os.path.join(d, x, y)) for x in dirs for y in ("system", "constant"))
        if "Allrun" in files and os.access(os.path.join(d, "Allrun"), os.X_OK) and isCase:
            found.append(os.path.relpath(d, base))
            dirs[:] = []
    rows = []
    try:
        doc = open(os.path.join(checkout, "doc", "case-setup.md")).read()
        doc = doc.split("## Tutorial Cases", 1)[1].split("\n## ", 1)[0]
        for line in doc.splitlines():
            cols = [c.strip() for c in line.strip().strip("|").split("|")]
            if len(cols) >= 2 and "`" in cols[0]:
                for pat in re.findall(r"`([^`]+)`", cols[0]):
                    rows.append((pat.rstrip("/"), cols[1]))
    except (OSError, IndexError):
        pass
    out, said = [], set()
    for path in sorted(found, key=str.lower):
        what = ""
        for pat, desc in rows:
            if fnmatch.fnmatch(path, pat) or fnmatch.fnmatch(path, pat + "/*"):
                what = "" if desc in said else desc
                said.add(desc)
                break
        out.append((path, what))
    return out


def record(copy, checkout, tutorial):
    branch = git(checkout, "symbolic-ref", "--short", "-q", "HEAD") or None
    rec = {"checkout": os.path.realpath(checkout), "branch": branch,
           "commit": git(checkout, "rev-parse", "HEAD") or None, "tutorial": tutorial,
           "copied_utc": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")}
    with open(os.path.join(copy, RECORD), "w") as f:
        json.dump(rec, f, indent=2)
        f.write("\n")


def load(case):
    try:
        return json.load(open(os.path.join(case, RECORD)))
    except (OSError, ValueError):
        return None


def where(rec):
    """'<branch> @ <commit7>' for a record."""
    c = (rec.get("commit") or "?")[:7]
    return "%s @ %s" % (rec["branch"], c) if rec.get("branch") else "commit " + c


def stale(rec):
    """Why the recorded checkout no longer matches the copy, or ''."""
    co = rec.get("checkout") or ""
    if not os.path.isfile(os.path.join(co, "pgf")):
        return "its checkout %s is gone" % tilde(co)
    branch = git(co, "symbolic-ref", "--short", "-q", "HEAD")
    head = git(co, "rev-parse", "HEAD")
    if rec.get("branch") and branch != rec["branch"]:
        return "%s is now on %s, not %s" % (tilde(co), branch or "a detached HEAD", rec["branch"])
    if rec.get("commit") and head and head != rec["commit"]:
        return "%s has moved on since the copy (%s, now %s)" % (
            rec.get("branch") or "the checkout", rec["commit"][:7], head[:7])
    return ""


def check(case):
    rec = load(case)
    if not rec:
        return
    info = "copy of %s from %s (%s)" % (rec.get("tutorial") or "?", where(rec), tilde(rec.get("checkout") or "?"))
    why = stale(rec)
    warn = ""
    if why:
        warn = "%s; the case files may differ from the checkout's now; fresh copy: pgf copy %s --force" % (
            why, rec.get("tutorial") or "<tutorial>")
    print("\t".join((rec.get("checkout") or "", info, warn)))


def list_copies(root):
    if not os.path.isdir(root):
        return
    byBranch = {}
    for d, dirs, files in os.walk(root):
        if RECORD in files:
            dirs[:] = []
            rel = os.path.relpath(d, root)
            branch, _, name = rel.partition(os.sep)
            byBranch.setdefault(branch, []).append((name or rel, load(d) or {}))
        elif os.path.relpath(d, root).count(os.sep) >= 5:
            dirs[:] = []
        else:
            dirs.sort()
    if not byBranch:
        return
    w = max(len(n) for v in byBranch.values() for n, _ in v)
    for branch in sorted(byBranch):
        print("  %s" % branch)
        for name, rec in byBranch[branch]:
            why = stale(rec) if rec else "no readable %s" % RECORD
            print("    %-*s  %s%s" % (w, name, where(rec) if rec else "?",
                                     ("  stale: " + why) if why else ""))


def main(argv):
    cmd = argv[1] if len(argv) > 1 else ""
    if cmd == "tutorials" and len(argv) == 3:
        for path, what in tutorials(argv[2]):
            print("%s\t%s" % (path, what))
    elif cmd == "record" and len(argv) == 5:
        record(*argv[2:5])
    elif cmd == "check" and len(argv) == 3:
        check(argv[2])
    elif cmd == "list" and len(argv) == 3:
        list_copies(argv[2])
    else:
        sys.stderr.write(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
