#!/bin/bash
# Fresh-install Python modules (#371): every module a tutorial script imports
# is in the standard library, the tutorials themselves, yade, or
# PGF_YADE_PY_MODULES (pack.sh), whose packages are installed by pgf pack and
# listed in doc/install.md.

. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"
. "$PGF_ROOT/utilities/pgf/pack.sh"

listed=" $(for m in $PGF_YADE_PY_MODULES; do printf '%s ' "${m%%:*}"; done)"
unlisted="$(python3 -I - "$PGF_ROOT/tutorials" <<'PY'
import ast, pathlib, sys
root = pathlib.Path(sys.argv[1])
files = list(root.rglob("*.py"))
local = {f.stem for f in files} | {"yade"}
seen = set()
for f in files:
    for n in ast.walk(ast.parse(f.read_text(), str(f))):
        if isinstance(n, ast.Import):
            seen |= {a.name.split(".")[0] for a in n.names}
        elif isinstance(n, ast.ImportFrom) and n.level == 0 and n.module:
            seen.add(n.module.split(".")[0])
print(" ".join(sorted(seen - local - set(sys.stdlib_module_names))))
PY
)" || die "cannot parse the tutorial scripts"
missing=""
for m in $unlisted; do [[ "$listed" == *" $m "* ]] || missing+=" $m"; done
check "tutorial imports are in PGF_YADE_PY_MODULES${missing:+ (missing:$missing)}" [ -z "$missing" ]

for m in $PGF_YADE_PY_MODULES; do
  check "${m#*:} is in PGF_PACK_YADE_DEPS" grep -qw -- "${m#*:}" <<<"$PGF_PACK_YADE_DEPS"
  check "${m#*:} is in doc/install.md" grep -qw -- "${m#*:}" "$PGF_ROOT/doc/install.md"
done

finish
