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
# Inside a pgf pack image there is no git index; the manifest has the pin.
pgf_coupling_pin() {
  local pin
  pin="$(git -C "$PGF_ROOT" ls-files -s -- submodules/Foam-Yade 2>/dev/null | awk '$1 == "160000" {print $2}')"
  [ -z "$pin" ] && [ -r "${PGF_IMAGE_MANIFEST:-}" ] &&
    pin="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("foam_yade_pin") or "")' "$PGF_IMAGE_MANIFEST")"
  printf '%s\n' "$pin"
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

# pgf_coupling_api_hash_at REPO COMMIT — the API hash of the coupling headers
#   at COMMIT in the Foam-Yade git repo REPO; fails if COMMIT is not there.
pgf_coupling_api_hash_at() {
  local repo="$1" commit="$2" h p="pkg/openfoam/coupling/FoamYade"
  git -C "$repo" cat-file -e "$commit:$p/FoamYade.H" 2>/dev/null || return 1
  for h in "${PGF_COUPLING_HEADERS[@]}"; do
    printf '%s\n' "$h"
    git -C "$repo" show "$commit:$p/$h" 2>/dev/null
  done | sha1sum | cut -c1-40
}

# pgf_yade_commit BIN — the Foam-Yade commit yade reports (yade -v: git-XXXX),
#   cached per binary path, size and mtime; empty when it reports none.
pgf_yade_commit() {
  local bin id cache out
  bin="$(readlink -f "$1")" || return 1
  id="$(stat -c '%s:%Y' "$bin" 2>/dev/null)" || return 1
  cache="${XDG_CACHE_HOME:-${HOME:-/tmp}/.cache}/pgf/yade-commit/$(printf '%s:%s' "$bin" "$id" | sha1sum | cut -c1-16)"
  if [ -f "$cache" ]; then cat "$cache"; return 0; fi
  out="$(timeout 60 "$bin" -v 2>/dev/null)"
  [[ "$out" =~ git-([0-9a-f]{7,40}) ]] && out="${BASH_REMATCH[1]}" || out=""
  mkdir -p "${cache%/*}" 2>/dev/null && printf '%s\n' "$out" >"$cache" 2>/dev/null
  printf '%s\n' "$out"
}

# pgf_yade_check BIN TRUNK — "LEVEL message" lines comparing the yade binary
#   with the pin and with TRUNK's coupling API: other API FAIL, other commit WARN.
pgf_yade_check() {
  local bin="$1" trunk="$2" commit pin repo want have
  commit="$(pgf_yade_commit "$bin")"
  pin="$(pgf_coupling_pin)"
  if [ -z "$commit" ]; then
    echo "WARN yade $bin reports no git commit; cannot check it against the Foam-Yade pin"
    return 0
  fi
  if [ -n "$pin" ] && [[ "$pin" == "$commit"* ]]; then
    echo "OK   yade $bin was built from the pinned Foam-Yade ${pin:0:10}"
    return 0
  fi
  want="$(pgf_coupling_api_hash "$trunk")"
  for repo in "$trunk" "$PGF_ROOT/submodules/Foam-Yade"; do
    have="$(pgf_coupling_api_hash_at "$repo" "$commit")" && break
  done
  if [ -z "$have" ]; then
    echo "WARN yade $bin was built from Foam-Yade $commit, not found in $trunk; cannot compare its coupling API (pin ${pin:0:10})"
  elif [ -n "$want" ] && [ "$have" != "$want" ]; then
    echo "FAIL yade $bin was built from Foam-Yade $commit, whose coupling API differs from $trunk (pin ${pin:0:10}); rebuild yade at the pin or set YADE_BIN"
    return 1
  else
    echo "WARN yade $bin was built from Foam-Yade $commit, not the pin ${pin:0:10}; same coupling API"
  fi
  return 0
}
