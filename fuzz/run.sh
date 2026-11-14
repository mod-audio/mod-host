#!/bin/sh
# Runs each named harness for FUZZ_TIME seconds over a scratch copy of its seed corpus, so a run never
# writes into the committed one. Any crash, leak or sanitizer report fails the run and leaves the artifact
# in $FUZZ_BUILD/artifacts.
set -eu
build=${FUZZ_BUILD:-build/fuzz}
secs=${FUZZ_TIME:-60}
mkdir -p "$build/artifacts"
for h in "$@"; do
    work="$build/work-$h"
    rm -rf "$work"
    mkdir -p "$work"
    cp -r "fuzz/corpus/$h/." "$work/"
    echo "fuzz_$h: ${secs}s"
    "$build/fuzz_$h" -max_total_time="$secs" -max_len=4096 -timeout=10 -rss_limit_mb=2048 \
        -artifact_prefix="$build/artifacts/$h-" "$work"
done
