#!/usr/bin/env bash
set -euo pipefail

# ASan + UBSan on macOS/Linux; Linux ASan also enables LeakSanitizer.
if (( $# != 1 )); then
    printf 'Usage: %s CLEAN_FIXTURE_DIRECTORY\n' "$0" >&2
    exit 2
fi

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
fixture=$1
[[ -d $fixture ]] || { printf 'Fixture directory missing\n' >&2; exit 2; }
build="$root/build/sanitizers"
os=$(uname -s)

cmake -S "$root" -B "$build" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_TESTING=ON \
    '-DCMAKE_C_FLAGS=-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined' \
    '-DCMAKE_CXX_FLAGS=-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined' \
    '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build "$build" --parallel 4

case "$os" in
    Linux) export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 ;;
    Darwin) export ASAN_OPTIONS=halt_on_error=1 ;;
    *) printf 'Unsupported OS: %s\n' "$os" >&2; exit 2 ;;
esac
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1

ctest --test-dir "$build" --output-on-failure
cmake --install "$build" --prefix "$build/install"
"$build/install/bin/mangen" "$fixture" --sha256 --threads 4 \
    > "$build/sanitizer-stdout.txt" \
    2> "$build/sanitizer-stderr.txt"

if [[ -s "$build/sanitizer-stderr.txt" ]]; then
    printf 'Unexpected stderr under sanitizers:\n' >&2
    sed -n '1,40p' "$build/sanitizer-stderr.txt" >&2
    exit 1
fi

if [[ $os == Darwin ]] && command -v leaks >/dev/null 2>&1; then
    printf 'macOS: optional independent leak inspection: leaks -atExit -- <program> ...\n'
fi
printf 'ASan/UBSan PASSED on %s. Linux LSan is enabled via ASAN_OPTIONS.\n' "$os"
