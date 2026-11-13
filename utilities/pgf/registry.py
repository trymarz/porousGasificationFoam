#!/usr/bin/env python3
"""pgf build registry (#264).

Builds are found by scanning $PGF_BUILD_ROOT/*/.pgf-build.json; there is no
index to go stale. Each checkout that built something keeps one small file,
$PGF_BUILD_ROOT/.pgf/workspaces/<hash>.json, naming its path and the key it
last built per variant, so `pgf ls` can say which checkouts use which build.

  registry.py register ROOT CHECKOUT VARIANT KEY BRANCH
  registry.py ls ROOT [--json] [--builds-only] [--current KEY ...]
  registry.py status CONF_JSON ROOT CHECKOUT [VARIANT=KEY ...]
  registry.py resolve ROOT VARIANT SEL       print the prefix SEL names
  registry.py record CASE ROOT PREFIX SOURCE YADE_BIN YADE_COMMIT STARTED RC CMD ...
  registry.py gc ROOT [--keep N] [--yes] [--dry-run] [--json] [--protect KEY ...]

A run holds a shared flock on ROOT/<key>.use.lock and a build an exclusive one
on ROOT/<key>.lock; gc skips a build while either is held.
"""
import datetime
import fcntl
import hashlib
import json
import os
import shutil
import sys


def now():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def ws_dir(root):
    return os.path.join(root, ".pgf", "workspaces")


def ws_file(root, checkout):
    h = hashlib.sha1(os.path.realpath(checkout).encode()).hexdigest()[:16]
    return os.path.join(ws_dir(root), h + ".json")


def load(path):
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def write_atomic(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = "%s.tmp.%d" % (path, os.getpid())
    with open(tmp, "w") as f:
        json.dump(data, f, indent=2)
        f.write("\n")
    os.replace(tmp, path)


def register(root, checkout, variant, key, branch):
    path = ws_file(root, checkout)
    ws = load(path) or {}
    ws.update({"path": os.path.realpath(checkout), "branch": branch or None, "updated_utc": now()})
    ws.setdefault("builds", {})[variant] = key
    write_atomic(path, ws)


def workspaces(root):
    out = []
    d = ws_dir(root)
    for name in sorted(os.listdir(d)) if os.path.isdir(d) else []:
        ws = load(os.path.join(d, name)) if name.endswith(".json") else None
        if ws and ws.get("path"):
            ws["exists"] = os.path.isdir(ws["path"])
            out.append(ws)
    return out


def dir_size(path):
    total = 0
    for dp, _, files in os.walk(path):
        for f in files:
            try:
                total += os.lstat(os.path.join(dp, f)).st_size
            except OSError:
                pass
    return total


def lock_held(path, shared_ok=False):
    """True if someone holds PATH's flock (any lock, or only an exclusive one)."""
    try:
        fd = os.open(path, os.O_RDONLY)
    except OSError:
        return False
    try:
        fcntl.flock(fd, (fcntl.LOCK_SH if shared_ok else fcntl.LOCK_EX) | fcntl.LOCK_NB)
        return False
    except OSError:
        return True
    finally:
        os.close(fd)


def in_use(root, key):
    return lock_held(os.path.join(root, key + ".use.lock"))


def building(root, key):
    return lock_held(os.path.join(root, key + ".lock"))


def builds(root, current=()):
    wss = workspaces(root)
    out = []
    for name in sorted(os.listdir(root)) if os.path.isdir(root) else []:
        prefix = os.path.join(root, name)
        meta = load(os.path.join(prefix, ".pgf-build.json"))
        if not os.path.isdir(prefix) or name.startswith("."):
            continue
        used_by = [w["path"] for w in wss if name in (w.get("builds") or {}).values()]
        out.append({
            "key": name,
            "prefix": prefix,
            "complete": meta is not None,
            "current": name in current,
            "variant": (meta or {}).get("variant"),
            "built_utc": (meta or {}).get("built_utc"),
            "checkout": (meta or {}).get("checkout") or {"path": (meta or {}).get("source_tree")},
            "used_by": used_by,
            "in_use": in_use(root, name),
            "building": building(root, name),
            "git_head": (meta or {}).get("git_head"),
            "foam_yade_pin": ((meta or {}).get("foam_yade") or {}).get("pin"),
            "mtime": os.path.getmtime(prefix),
            "size_bytes": dir_size(prefix),
        })
    return out


def human(n):
    for unit in ("B", "K", "M", "G"):
        if n < 1024 or unit == "G":
            return "%.0f%s" % (n, unit) if unit == "B" else "%.1f%s" % (n, unit)
        n /= 1024.0


def cmd_ls(argv):
    root, rest = argv[0], argv[1:]
    as_json = "--json" in rest
    only = "--builds-only" in rest
    current = rest[rest.index("--current") + 1:] if "--current" in rest else []
    bs = builds(root, current)
    if as_json:
        out = {"build_root": root, "builds": bs}
        if not only:
            out["workspaces"] = workspaces(root)
        print(json.dumps(out, indent=2))
        return 0
    if not only:
        wss = workspaces(root)
        print("workspaces:" if wss else "no workspaces registered")
        for w in wss:
            keys = ", ".join("%s %s" % (v, k) for v, k in sorted((w.get("builds") or {}).items()))
            print("  %s%s%s  %s" % (w["path"], "@" + w["branch"] if w.get("branch") else "",
                                    "" if w["exists"] else " (gone)", keys))
    if not bs:
        print("no builds under %s" % root)
        return 0
    print("builds under %s:" % root)
    for b in bs:
        mark = "*" if b["current"] else " "
        state = "" if b["complete"] else "  (incomplete)"
        state += "  [in use]" if b["in_use"] else ""
        state += "  [building]" if b["building"] else ""
        where = b["checkout"].get("worktree") or os.path.basename(b["checkout"].get("path") or "") or "?"
        branch = b["checkout"].get("branch")
        print("%s %-44s %-6s %-20s %6s  %s%s%s" % (
            mark, b["key"], b["variant"] or "?", b["built_utc"] or "-", human(b["size_bytes"]),
            where, "@" + branch if branch else "", state))
        for u in b["used_by"]:
            print("      used by %s" % u)
    return 0


def cmd_status(argv):
    conf, root, checkout, pairs = argv[0], argv[1], argv[2], argv[3:]
    out = json.loads(conf)
    out["checkout"] = os.path.realpath(checkout)
    out["build_root"] = root or None
    out["builds"] = {}
    for p in pairs:
        variant, _, key = p.partition("=")
        prefix = os.path.join(root, key) if root and key else None
        meta = load(os.path.join(prefix, ".pgf-build.json")) if prefix else None
        out["builds"][variant] = {"key": key or None, "prefix": prefix, "complete": meta is not None,
                                  "built_utc": (meta or {}).get("built_utc")}
    print(json.dumps(out, indent=2))
    return 0


def resolve(root, variant, sel):
    """The prefix SEL names: a prefix path, a key or unique key prefix, a commit
    (newest build of VARIANT from it) or a worktree name/path (its last build)."""
    if os.path.isfile(os.path.join(sel, ".pgf-build.json")):
        return os.path.realpath(sel), None
    bs = [b for b in builds(root) if b["complete"]]
    if any(b["key"] == sel for b in bs):
        return os.path.join(root, sel), None
    hits = [b for b in bs if b["key"].startswith(sel)]
    if len(hits) == 1:
        return hits[0]["prefix"], None
    if len(hits) > 1:
        return None, "'%s' matches %d builds: %s" % (sel, len(hits), ", ".join(b["key"] for b in hits))
    if len(sel) >= 7 and all(c in "0123456789abcdef" for c in sel.lower()):
        hits = [b for b in bs if (b["git_head"] or "").startswith(sel.lower()) and b["variant"] == variant]
        if hits:
            return max(hits, key=lambda b: b["built_utc"] or "")["prefix"], None
    for w in workspaces(root):
        if sel in (w["path"], os.path.basename(w["path"])) or os.path.realpath(sel) == w["path"]:
            key = (w.get("builds") or {}).get(variant)
            if key and os.path.isfile(os.path.join(root, key, ".pgf-build.json")):
                return os.path.join(root, key), None
            return None, "worktree %s has no complete %s build" % (w["path"], variant)
    return None, "no build matches '%s' (a key, key prefix, commit, worktree or prefix path; see pgf builds)" % sel


def cmd_resolve(argv):
    prefix, err = resolve(*argv)
    if err:
        print(err, file=sys.stderr)
        return 1
    print(prefix)
    return 0


def cmd_gc(argv):
    root, rest = argv[0], argv[1:]
    keep = int(rest[rest.index("--keep") + 1]) if "--keep" in rest else 2
    yes, dry, as_json = "--yes" in rest, "--dry-run" in rest, "--json" in rest
    protect = set(rest[rest.index("--protect") + 1:]) if "--protect" in rest else set()
    referenced = {k for w in workspaces(root) if w["exists"] for k in (w.get("builds") or {}).values()}
    plan, kept = [], []
    for b in sorted(builds(root), key=lambda b: b["mtime"], reverse=True):
        why = ("this checkout" if b["key"] in protect else "in use" if b["in_use"] else
               "building" if b["building"] else "a workspace's last build" if b["key"] in referenced else None)
        if why is None and keep > 0 and b["complete"]:
            keep -= 1
            why = "--keep"
        (kept if why else plan).append(dict(b, why=why))
    if as_json:
        print(json.dumps({"build_root": root, "remove": plan, "keep": kept, "dry_run": dry or not yes}, indent=2))
    else:
        for b in kept:
            print("keep   %s (%s)" % (b["key"], b["why"]))
        for b in plan:
            print("remove %s (%s)" % (b["key"], human(b["size_bytes"])))
    if not plan or dry:
        return 0
    if not yes:
        if not sys.stdin.isatty():
            print("pgf gc: not removing %d build(s) without --yes (no terminal to ask)" % len(plan), file=sys.stderr)
            return 2
        sys.stderr.write("remove %d build(s), %s? [y/N] " % (len(plan), human(sum(b["size_bytes"] for b in plan))))
        if sys.stdin.readline().strip().lower() not in ("y", "yes"):
            return 1
    rc = 0
    for b in plan:
        rc |= remove(root, b["key"])
    return rc


def remove(root, key):
    """Delete one build under exclusive locks, so no run or build starts on it meanwhile."""
    fds = []
    try:
        for name in (key + ".use.lock", key + ".lock"):
            fd = os.open(os.path.join(root, name), os.O_RDWR | os.O_CREAT, 0o644)
            fds.append(fd)
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        shutil.rmtree(os.path.join(root, key))
        for name in (key + ".use.lock", key + ".lock"):
            try:
                os.unlink(os.path.join(root, name))
            except OSError:
                pass
        print("removed %s" % key, file=sys.stderr)
        return 0
    except OSError as e:
        print("pgf gc: kept %s: %s" % (key, e), file=sys.stderr)
        return 1
    finally:
        for fd in fds:
            os.close(fd)


def record(case, root, prefix, source, yade_bin, yade_commit, started, rc, *cmd):
    """Write CASE/.pgf-run.json and append the same record to ROOT/.pgf/runs.jsonl."""
    meta = load(os.path.join(prefix, ".pgf-build.json")) or {}
    rec = {
        "case": os.path.realpath(case), "command": list(cmd),
        "started_utc": started, "finished_utc": now(), "exit_status": int(rc),
        "build": {"key": os.path.basename(prefix), "prefix": prefix, "source": source,
                  "variant": meta.get("variant"), "git_head": meta.get("git_head")},
        "yade": None,
    }
    if yade_bin:
        rec["yade"] = {"bin": yade_bin, "commit": yade_commit or None, "foam_yade_pin": (meta.get("foam_yade") or {}).get("pin")}
    write_atomic(os.path.join(case, ".pgf-run.json"), rec)
    if root and os.path.isdir(root):
        os.makedirs(os.path.join(root, ".pgf"), exist_ok=True)
        with open(os.path.join(root, ".pgf", "runs.jsonl"), "a") as f:
            f.write(json.dumps(rec) + "\n")


def main(argv):
    if not argv:
        print(__doc__, file=sys.stderr)
        return 2
    cmd, rest = argv[0], argv[1:]
    if cmd == "register" and len(rest) == 5:
        register(*rest)
        return 0
    if cmd == "ls" and rest:
        return cmd_ls(rest)
    if cmd == "resolve" and len(rest) == 3:
        return cmd_resolve(rest)
    if cmd == "record" and len(rest) >= 9:
        record(*rest)
        return 0
    if cmd == "gc" and rest:
        return cmd_gc(rest)
    if cmd == "status" and len(rest) >= 3:
        return cmd_status(rest)
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
