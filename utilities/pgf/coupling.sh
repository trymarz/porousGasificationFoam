#!/bin/bash
# coupling.sh — Foam-Yade coupling stamp and API checks (#257). Source-only;
# shared by build.sh, pgf and buildCoupling.sh. Needs PGF_ROOT.

PGF_COUPLING_STAMP=".foamyade-coupling.json"
PGF_COUPLING_HEADERS=(FoamYade.H meshtree/meshTree.H commYade/yadeComm.H)

pgf_coupling_dir() {
  printf '%s/pkg/openfoam/coupling/FoamYade' "$1"
}

# pgf_coupling_api_hash TRUNK — sha1 of the coupling headers; empty if absent.
pgf_coupling_api_hash() {
  local dir h
  dir="$(pgf_coupling_dir "$1")"
  [ -f "$dir/FoamYade.H" ] || return 1
  for h in "${PGF_COUPLING_HEADERS[@]}"; do
    printf '%s\n' "$h"
    [ -f "$dir/$h" ] && cat "$dir/$h"
  done | sha1sum | cut -c1-40
}

# pgf_coupling_pin — the Foam-Yade commit this tree's index pins.
pgf_coupling_pin() {
  git -C "$PGF_ROOT" ls-files -s -- submodules/Foam-Yade 2>/dev/null | awk '$1 == "160000" {print $2}'
}

pgf_coupling_trunk_head() {
  git -C "$1" rev-parse HEAD 2>/dev/null
}

# pgf_coupling_stamp_field LIBDIR FIELD — one field of LIBDIR's stamp.
pgf_coupling_stamp_field() {
  python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get(sys.argv[2]) or "")' \
    "$1/$PGF_COUPLING_STAMP" "$2" 2>/dev/null
}

# pgf_coupling_write_stamp LIBDIR TRUNK — record what built the libs there.
pgf_coupling_write_stamp() {
  python3 - "$1/$PGF_COUPLING_STAMP" "$(pgf_coupling_trunk_head "$2")" "$2" \
    "$(pgf_coupling_api_hash "$2")" <<'PY'
import json, os, sys
path, commit, trunk, api = sys.argv[1:5]
with open(path + ".tmp", "w") as f:
    json.dump({"foamYadeCommit": commit or None, "yadeTrunk": trunk, "apiHash": api}, f, indent=2)
    f.write("\n")
os.replace(path + ".tmp", path)
PY
}

pgf_coupling_rebuild_cmd() {
  printf '%s/pgf exec --bare --yade -- %s/utilities/pgf/buildCoupling.sh --libdir %s' \
    "$PGF_ROOT" "$PGF_ROOT" "$1"
}

# pgf_coupling_check LIBDIR TRUNK — print "LEVEL message" lines (OK, WARN,
#   FAIL); return 1 when any is FAIL. A missing stamp is a WARN, not a FAIL.
pgf_coupling_check() {
  local libdir="$1" trunk="$2" want have pin head rc=0
  want="$(pgf_coupling_api_hash "$trunk")" || {
    echo "FAIL no coupling headers under YADE_TRUNK=$trunk"
    return 1
  }
  if [ ! -f "$libdir/$PGF_COUPLING_STAMP" ]; then
    echo "WARN no coupling stamp in $libdir; cannot tell which trunk built the libs (rebuild: $(pgf_coupling_rebuild_cmd "$libdir"))"
  else
    have="$(pgf_coupling_stamp_field "$libdir" apiHash)"
    if [ "$have" = "$want" ]; then
      echo "OK   coupling libs in $libdir match the headers in $trunk"
    else
      echo "FAIL coupling libs in $libdir were built from other headers (stamp ${have:0:10}, trunk ${want:0:10}, built from $(pgf_coupling_stamp_field "$libdir" yadeTrunk)); rebuild coupling: $(pgf_coupling_rebuild_cmd "$libdir")"
      rc=1
    fi
  fi
  pin="$(pgf_coupling_pin)"
  head="$(pgf_coupling_trunk_head "$trunk")"
  if [ -n "$pin" ] && [ -n "$head" ] && [ "$pin" != "$head" ]; then
    echo "WARN YADE_TRUNK is at ${head:0:10}, the submodule pin is ${pin:0:10}"
  fi
  return "$rc"
}
