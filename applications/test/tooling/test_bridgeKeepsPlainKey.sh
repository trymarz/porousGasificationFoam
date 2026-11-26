#!/bin/bash
# The PgfDemBridge sources are part of the DEM build key only: a change under
# submodules/PgfDemBridge/src keeps the non-DEM key and changes the --yade key,
# so checking out the submodule (doc/install.md, step 3) does not orphan the
# plain build. The change is an untracked probe file, removed on exit.

. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"

src="$PGF_ROOT/submodules/PgfDemBridge/src"
[ -d "$src" ] || die "$src is absent; check out the submodule first"

plain="$(tree_prefix)" || die "cannot resolve the non-DEM prefix"
dem="$(tree_prefix --yade)" || die "cannot resolve the DEM prefix"

probe="$src/pgfToolingProbe.hpp"
[ -e "$probe" ] && die "$probe already exists"
trap 'rm -f "$probe"; rm -rf "$TEST_TMP"' EXIT
echo "// tooling test probe: changes the bridge sources only" >"$probe"

check "a bridge change keeps the non-DEM key (${plain##*/})" [ "$(tree_prefix)" = "$plain" ]
check "a bridge change gives a new DEM key" [ "$(tree_prefix --yade)" != "$dem" ]

rm -f "$probe"
check "probe removed; DEM key is back to ${dem##*/}" [ "$(tree_prefix --yade)" = "$dem" ]
finish
