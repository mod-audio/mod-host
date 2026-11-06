#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# one daemon on a port (tests/bind-test.c): each arm breaks the rule in a scratch copy of the sources, and the test built
# over that copy must fail on each.
#
#   bind-sabotage.sh "<cc>" "<cflags>"
set -eu
cc=$1
cflags=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
failed=0

# <what> <the code, as sed matches it> <a mutant>
sabotage() {
    what=$1 fixed=$2 mutant=$3
    rm -rf "$work/tree"
    mkdir -p "$work/tree"
    cp -r src tests "$work/tree/"
    grep -qF "$(printf '%s' "$fixed" | sed 's/\\//g')" "$work/tree/src/socket.c" || { echo "FAIL $what: the code is not in src/socket.c"; failed=1; return; }
    sed -i "s/$fixed/$mutant/" "$work/tree/src/socket.c"
    # shellcheck disable=SC2086
    (cd "$work/tree/tests" && $cc $cflags -I../src -o ../bind-test bind-test.c -lpthread) \
        || { echo "FAIL $what: the sabotaged tree does not build"; failed=1; return; }
    if (cd "$work/tree" && ./bind-test) >"$work/log" 2>&1; then
        echo "FAIL $what: bind-test passes"
        failed=1
    else
        echo "ok   $what: bind-test fails ($(grep -m1 '^FAILED' "$work/log" | head -c 120))"
    fi
}

sabotage "a second daemon may listen on the port a running one holds" \
    'setsockopt(g_serverfd, SOL_SOCKET, SO_REUSEADDR' 'setsockopt(g_serverfd, SOL_SOCKET, SO_REUSEPORT'
sabotage "a port the daemon before left in TIME_WAIT is not taken again" \
    'setsockopt(g_serverfd, SOL_SOCKET, SO_REUSEADDR, \&value, sizeof(value));' ''

exit $failed
