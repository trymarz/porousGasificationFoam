#!/usr/bin/env python3
"""pgf build registry (#264).

Builds are found by scanning $PGF_BUILD_ROOT/*/.pgf-build.json; there is no
index to go stale. Each checkout that built something keeps one small file,
$PGF_BUILD_ROOT/.pgf/workspaces/<hash>.json, naming its path and the key it
last built per variant, so `pgf ls` can say which checkouts use which build.

  registry.py register ROOT CHECKOUT VARIANT KEY BRANCH
  registry.py ls ROOT [--json] [--current KEY ...]
  registry.py status CONF_JSON ROOT CHECKOUT [VARIANT=KEY ...]
"""
import datetime
import hashlib
import json
import os
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
    current = rest[rest.index("--current") + 1:] if "--current" in rest else []
    bs = builds(root, current)
    if as_json:
        print(json.dumps({"build_root": root, "builds": bs, "workspaces": workspaces(root)}, indent=2))
        return 0
    if not bs:
        print("no builds under %s" % root)
        return 0
    print("builds under %s:" % root)
    for b in bs:
        mark = "*" if b["current"] else " "
        state = "" if b["complete"] else "  (incomplete)"
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
    if cmd == "status" and len(rest) >= 3:
        return cmd_status(rest)
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
