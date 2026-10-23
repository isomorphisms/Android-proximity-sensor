#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/proximity-host.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
cc=${CC:-cc}
"$cc" -std=c17 -O2 -Wall -Wextra -Werror -Wpedantic -Wshadow \
    -I"$root/model" \
    "$root/tests/proximity_model_test.c" -lm -o "$build_dir/proximity-model-test"
"$build_dir/proximity-model-test"
