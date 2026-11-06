#!/usr/bin/env bash
set -euo pipefail

# Linux/macOS build matrix. No downloads are performed by this script.
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
os=$(uname -s)
case "$os" in
    Darwin|Linux) ;;
    *) printf 'Unsupported OS for this matrix: %s\n' "$os" >&2; exit 2 ;;
esac

for program in cmake ctest; do
    command -v "$program" >/dev/null 2>&1 || {
        printf 'Missing tool: %s\n' "$program" >&2
        exit 2
    }
done

compilers=()
if [[ -n ${CC:-} ]]; then
    compilers+=("$CC")
else
    for compiler in clang gcc cc; do
        if command -v "$compiler" >/dev/null 2>&1; then
            compilers+=("$(command -v "$compiler")")
        fi
    done
fi
if (( ${#compilers[@]} == 0 )); then
    printf 'No C compiler found.\n' >&2
    exit 2
fi

failed=0
index=0
for compiler in "${compilers[@]}"; do
    index=$((index + 1))
    for testing in OFF ON; do
        build="$root/build/portability-$os-$index-$testing"
        printf '\n=== %s, %s, BUILD_TESTING=%s ===\n' "$os" "$compiler" "$testing"
        if ! cmake -S "$root" -B "$build" \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_C_COMPILER="$compiler" \
            -DBUILD_TESTING="$testing"; then
            failed=1
            continue
        fi
        if ! cmake --build "$build" --parallel 4; then
            failed=1
            continue
        fi
        if [[ $testing == ON ]]; then
            if ! ctest --test-dir "$build" --output-on-failure; then
                failed=1
            fi
        fi
        if ! cmake --install "$build" --prefix "$build/install"; then
            failed=1
            continue
        fi
        if [[ -x "$build/install/bin/mangen" ]]; then
            "$build/install/bin/mangen" --help >/dev/null || failed=1
        else
            printf 'FAIL: installed executable is missing: %s\n' \
                "$build/install/bin/mangen" >&2
            failed=1
        fi
    done
done

if (( failed != 0 )); then
    printf 'Portability matrix FAILED on %s.\n' "$os" >&2
    exit 1
fi
printf 'Portability matrix PASSED on %s.\n' "$os"
