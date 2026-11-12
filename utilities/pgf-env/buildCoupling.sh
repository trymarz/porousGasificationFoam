#!/bin/bash
# buildCoupling.sh [--libdir DIR] — build Foam-Yade's coupling libs from
# $YADE_TRUNK into DIR (default $PGF_YADE_LIBDIR, else $FOAM_USER_LIBBIN) and
# stamp them (#257). Run it as: pgf-env --bare --yade -- <this script>.

PGF_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)"
. "$PGF_ROOT/utilities/pgf-env/coupling.sh"

die() { echo "buildCoupling.sh: $*" >&2; exit 2; }

libdir="${PGF_YADE_LIBDIR:-${FOAM_USER_LIBBIN:-}}"
case "${1:-}" in
--libdir) [ -n "${2:-}" ] || die "--libdir needs a directory"; libdir="$2" ;;
"") ;;
*) die "usage: buildCoupling.sh [--libdir DIR]" ;;
esac
[ -n "$libdir" ] || die "no lib dir: pass --libdir DIR or set PGF_YADE_LIBDIR"
command -v wmake >/dev/null || die "wmake not on PATH; run under pgf-env"
[ -n "${YADE_TRUNK:-}" ] || die "YADE_TRUNK is unset"
src="$(pgf_coupling_dir "$YADE_TRUNK")"
[ -f "$src/FoamYade.H" ] || die "no coupling sources under $src"

mkdir -p "$libdir" && libdir="$(cd "$libdir" && pwd -P)" || die "cannot create $libdir"
export FOAM_USER_LIBBIN="$libdir"
export LD_LIBRARY_PATH="$libdir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
rm -f "$libdir/$PGF_COUPLING_STAMP"
# libYadeFoam links libMeshTree, so meshtree goes first.
(cd "$src/meshtree" && wmake libso) &&
  (cd "$src/commYade" && wmake libso) &&
  (cd "$src" && wmake libso) || die "coupling build failed"
pgf_coupling_write_stamp "$libdir" "$YADE_TRUNK" || die "cannot write the stamp"
echo "buildCoupling.sh: built and stamped the coupling libs in $libdir" >&2
