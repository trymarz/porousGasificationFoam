#!/bin/bash
# gen-pgf-reference.sh — write doc/pgf-reference.md from pgf help, one heading
# per command and topic (#352). --check: exit 1 if the committed page is stale.
#   utilities/gen-pgf-reference.sh [--check]

set -eu
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
out="$root/doc/pgf-reference.md"

# pgfHelp ARGS — pgf help in a clean caller, so the page is the same on every machine.
pgfHelp() {
    env -i PATH=/usr/bin:/bin HOME=/nonexistent "$root/pgf" --color never help "$@"
}

# pages — the command and topic names, in the order pgf help lists them.
pages() {
    pgfHelp | sed -n '/Commands:$/,/^$/s/^  \([a-z-]*\) .*/\1/p;/^Help Topics:$/,/^$/s/^  \([a-z-]*\) .*/\1/p'
}

generate() {
    local p
    cat <<'EOF'
# pgf reference

Every page of `pgf help`, as plain text. **Generated** by `utilities/gen-pgf-reference.sh` from the pgf in this checkout; do not edit by hand. A tooling test (`applications/test/tooling/test_pgfCommands.sh`) fails when this page is stale. How pgf works, and when to use which command: [pgf guide](pgf.md).

EOF
    echo "- [pgf](#pgf)"
    for p in $(pages); do
        echo "- [pgf help $p](#pgf-help-$p)"
    done
    printf '\n## pgf\n\n```text\n'
    pgfHelp
    printf '```\n'
    for p in $(pages); do
        printf '\n## pgf help %s\n\n```text\n' "$p"
        pgfHelp "$p"
        printf '```\n'
    done
}

if [ "${1:-}" = --check ]; then
    if cmp -s <(generate) "$out"; then
        exit 0
    fi
    echo "doc/pgf-reference.md is stale: run utilities/gen-pgf-reference.sh" >&2
    exit 1
fi
generate >"$out"
