#!/bin/bash
#
# build.sh: deprecated shim (#274); the build driver moved to utilities/pgf/build.sh.
# what:    forwards to utilities/pgf/build.sh with the same arguments
# why:     keeps old scripts and habits working for a deprecation period
# instead: pgf build [options] (isolated), or utilities/pgf/build.sh (in place)

echo "build.sh: moved to utilities/pgf/build.sh; use: pgf build $*" >&2
exec "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)/utilities/pgf/build.sh" "$@"
