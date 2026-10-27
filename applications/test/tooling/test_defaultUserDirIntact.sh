#!/bin/bash
# An isolated build leaves every file in the default user dir
# ($PGF_DEFAULT_USER_DIR/platforms/*/{bin,lib}) byte-identical. A fresh
# --prefix forces wmake to install every target, so the build really writes.

. "$(dirname "${BASH_SOURCE[0]}")/toolingLib.sh"

default="$(default_user_dir)"
[ -n "$default" ] || die "cannot determine the default user dir"
dirs=("$default"/platforms/*/bin "$default"/platforms/*/lib)
tree_sums "${dirs[@]}" >"$TEST_TMP/before"
echo "  default user dir: $default ($(wc -l <"$TEST_TMP/before") files)"

fresh="$TEST_TMP/prefix"
"$PGF_ENV" --bare -- "$BUILD_SH" --prefix "$fresh" >"$TEST_TMP/build.log" 2>&1
rc=$?
check "build.sh --prefix exits 0 (rc=$rc)" [ "$rc" -eq 0 ]
check "the build installed into the fresh prefix" [ -x "$fresh/bin/porousGasificationFoam" ]
check "the fresh prefix is complete" [ -f "$fresh/.pgf-build.json" ]

tree_sums "${dirs[@]}" >"$TEST_TMP/after"
check "default user dir bin/ and lib/ unchanged" cmp -s "$TEST_TMP/before" "$TEST_TMP/after"
[ "$rc" -eq 0 ] || tail -30 "$TEST_TMP/build.log"
finish
