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
  printf '%s/pgf shell --bare --yade -- %s/utilities/pgf/buildCoupling.sh --libdir %s' \
    "$PGF_ROOT" "$PGF_ROOT" "$1"
}

# pgf_coupling_check LIBDIR TRUNK — print "LEVEL message" lines (OK, WARN,
#   FAIL); return 1 when any is FAIL. A missing stamp is a WARN, not a FAIL.
pgf_coupling_check() {
  local libdir="$1" trunk="$2" want have rc=0
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
  pgf_trunk_pin_check "$trunk"
  return "$rc"
}

# pgf_trunk_pin_check TRUNK — a WARN line when TRUNK's HEAD is not the
#   submodule pin: headers that differ from the yade binary give wrong data.
pgf_trunk_pin_check() {
  local trunk="$1" pin head own="$PGF_ROOT/submodules/Foam-Yade"
  [ -e "$trunk/.git" ] || return 0 # else git -C would read an enclosing repo
  pin="$(pgf_coupling_pin)" head="$(pgf_coupling_trunk_head "$trunk")"
  [ -n "$pin" ] && [ -n "$head" ] && [ "$pin" != "$head" ] || return 0
  if [ "$(cd "$trunk" 2>/dev/null && pwd -P)" = "$(cd "$own" 2>/dev/null && pwd -P)" ]; then
    echo "WARN submodules/Foam-Yade is at ${head:0:10}, off its pin ${pin:0:10}; fix: git submodule update submodules/Foam-Yade"
  else
    echo "WARN YADE_TRUNK is at ${head:0:10}, the submodule pin is ${pin:0:10} ($trunk); a --yade build would use other headers than the pinned yade"
  fi
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

# pgf_yade_mpy BIN — the mpy.py installed with yade BIN, if pgf finds it.
pgf_yade_mpy() {
  local bin f
  bin="$(readlink -f "$1")" || return 1
  for f in ${YADE_PYTHONPATH:+"$YADE_PYTHONPATH/yade/mpy.py"} "${bin%/*/*}"/lib/*/"${bin##*/}"/py/yade/mpy.py; do
    [ -f "$f" ] && printf '%s\n' "$f" && return 0
  done
  return 1
}

# pgf_yade_pin_repo PIN TRUNK — a Foam-Yade git repo that has PIN: TRUNK or
#   the submodule; fails when neither has it.
pgf_yade_pin_repo() {
  local repo
  for repo in "$2" "$PGF_ROOT/submodules/Foam-Yade"; do
    [ -e "$repo/.git" ] && git -C "$repo" cat-file -e "$1^{commit}" 2>/dev/null && printf '%s\n' "$repo" && return 0
  done
  return 1
}

# pgf_yade_relation REPO COMMIT PIN — how COMMIT sits against PIN, e.g.
#   "6 commits older than"; "not" when REPO cannot tell.
pgf_yade_relation() {
  local full n
  full="$(git -C "$1" rev-parse -q --verify "$2^{commit}" 2>/dev/null)" || { echo "not"; return; }
  if git -C "$1" merge-base --is-ancestor "$full" "$3"; then
    n="$(git -C "$1" rev-list --count "$full..$3")"
    echo "$n commit$([ "$n" = 1 ] || echo s) older than"
  elif git -C "$1" merge-base --is-ancestor "$3" "$full"; then
    n="$(git -C "$1" rev-list --count "$3..$full")"
    echo "$n commit$([ "$n" = 1 ] || echo s) newer than"
  else
    echo "not on the history of"
  fi
}

# pgf_yade_check BIN TRUNK — is yade BIN at the pin (yade -v, and its mpy.py vs
#   the pin's)? Another coupling API FAILs; off the pin or unknown WARNs, or FAILs
#   under PGF_YADE_PIN_REQUIRED=1 (pgf test dem) unless PGF_YADE_OFF_PIN=1.
pgf_yade_check() {
  local bin="$1" trunk="$2" commit pin repo="" mpy="" mpy_at="" want have r b t fix why
  commit="$(pgf_yade_commit "$bin")"
  pin="$(pgf_coupling_pin)"
  b="${bin/#"$HOME"/\~}" t="${trunk/#"$HOME"/\~}"
  fix="build yade at Foam-Yade ${pin:0:10} (doc/build.md, YADE DEM coupling), then pgf config set YADE_BIN=<that yade>"
  if [ -z "$pin" ] && [ -z "$commit" ]; then
    echo "WARN yade $b does not say which Foam-Yade commit built it (yade -v), and pgf does not know the pin here"
    return 0
  elif [ -z "$pin" ]; then
    echo "WARN pgf does not know the Foam-Yade pin here (no git index, no image manifest), so it cannot check yade $b"
    return 0
  fi
  if repo="$(pgf_yade_pin_repo "$pin" "$trunk")" && repo="$(pgf_yade_pin_repo "$pin" "$trunk")" && mpy="$(pgf_yade_mpy "$bin")"; then
    if git -C "$repo" show "$pin:py/mpy.py" 2>/dev/null | cmp -s - "$mpy"; then mpy_at=pin; else mpy_at=other; fi
  fi
  if [ "$mpy_at" = pin ] && [ -n "$commit" ] && [[ "$pin" != "$commit"* ]]; then
    echo "OK   yade $b has the mpy.py of the pinned Foam-Yade ${pin:0:10}; its version names $commit, set by hand when it was configured"
    return 0
  elif [ "$mpy_at" != other ] && [ -n "$commit" ] && [[ "$pin" == "$commit"* ]]; then
    echo "OK   yade $b was built from the pinned Foam-Yade ${pin:0:10}"
    return 0
  elif [ "$mpy_at" = pin ]; then
    echo "OK   yade $b does not report its commit, but has the mpy.py of the pinned Foam-Yade ${pin:0:10}"
    return 0
  fi
  if [ -n "$commit" ] && [[ "$pin" != "$commit"* ]]; then
    want="$(pgf_coupling_api_hash "$trunk")"
    for r in "$trunk" "$PGF_ROOT/submodules/Foam-Yade"; do
      have="$(pgf_coupling_api_hash_at "$r" "$commit")" && break
    done
    if [ -n "$have" ] && [ -n "$want" ] && [ "$have" != "$want" ]; then
      echo "FAIL yade $b was built from Foam-Yade $commit, whose coupling API differs from $t's: DEM runs would exchange wrong data; fix: $fix"
      return 1
    fi
  fi
  if [ -z "$commit" ]; then
    why="yade $b does not say which Foam-Yade commit built it (yade -v), so pgf cannot tell whether it is at the pin ${pin:0:10}"
  elif [[ "$pin" == "$commit"* ]]; then
    why="yade $b says it is the pinned Foam-Yade ${pin:0:10}, but its mpy.py differs from the pin's"
  elif [ -n "$repo" ]; then
    why="yade $b was built from Foam-Yade $commit, $(pgf_yade_relation "$repo" "$commit" "$pin") the pin ${pin:0:10}"
  else
    why="yade $b was built from Foam-Yade $commit, not the pin ${pin:0:10}"
  fi
  if [ "${PGF_YADE_PIN_REQUIRED:-0}" != 1 ]; then
    echo "WARN $why; DEM results may differ from the dem baselines; fix: $fix"
  elif [ "${PGF_YADE_OFF_PIN:-0}" = 1 ]; then
    echo "WARN $why; running anyway, as PGF_YADE_OFF_PIN=1 asks: a failure may be yade's, not the solver's"
  else
    echo "FAIL $why; the dem baselines need yade at the pin; fix: $fix"
    return 1
  fi
  return 0
}
