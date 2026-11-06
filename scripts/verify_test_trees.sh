#!/usr/bin/env bash
set -euo pipefail

# Recomputes every digest with the operating system's own SHA-2 utility.
# The generated test fixtures use safe single-line ASCII names by design.
if (( $# < 1 || $# > 2 )); then
    printf 'Usage: %s FIXTURE_ROOT [PATH_TO_MANGEN]\n' "$0" >&2
    exit 2
fi

base=${1%/}
binary=${2:-./build/install/bin/mangen}
if [[ $binary != /* ]]; then
    binary="$PWD/$binary"
fi
if [[ ! -f "$base/.mangen-fixture" ]] || \
   [[ $(< "$base/.mangen-fixture") != mangen-test-fixtures-v1 ]]; then
    printf 'Error: not a recognized mangen fixture root: %s\n' "$base" >&2
    exit 2
fi
if [[ ! -x $binary ]]; then
    printf 'Error: mangen executable not found: %s\n' "$binary" >&2
    exit 2
fi

if command -v shasum >/dev/null 2>&1; then
    sha_backend=shasum
elif command -v sha256sum >/dev/null 2>&1 && \
     command -v sha384sum >/dev/null 2>&1 && \
     command -v sha512sum >/dev/null 2>&1; then
    sha_backend=coreutils
else
    printf 'Error: need shasum or GNU sha256sum/sha384sum/sha512sum\n' >&2
    exit 2
fi

temporary=$(mktemp -d "${TMPDIR:-/tmp}/mangen-verify.XXXXXXXX")
trap 'rm -r "$temporary"' EXIT
LC_ALL=C
export LC_ALL
failed=0

for algorithm in 256 384 512; do
    for tree in clean restricted; do
        directory="$base/$tree"
        expected="$temporary/expected-$tree-$algorithm"
        : > "$expected"

        # Hash in batches: 6403 separate shasum processes would be very slow.
        if [[ $sha_backend == shasum ]]; then
            hash_command=(shasum -a "$algorithm")
        else
            hash_command=("sha${algorithm}sum")
        fi
        if find "$directory" \( -type f -o -type l \) -print0 \
            2> "$temporary/find-$tree-$algorithm.err" | \
           xargs -0 "${hash_command[@]}" \
            2> "$temporary/system-sha-$tree-$algorithm.err" | \
           awk -v prefix="$directory/" '
               {
                   digest=toupper($1)
                   full=substr($0, length($1)+3)
                   if (substr(full, 1, length(prefix)) != prefix) exit 2
                   relative=substr(full, length(prefix)+1)
                   print relative " : " digest
               }
           ' > "$expected"; then
            :
        elif [[ $tree == clean ]]; then
            printf 'FAIL: independent hash calculation failed on clean tree\n' >&2
            failed=1
        fi

        LC_ALL=C sort "$expected" -o "$expected"
        expected_count=$(wc -l < "$expected" | tr -d ' ')
        if [[ $tree == clean ]] && (( expected_count != 6403 )); then
            printf 'FAIL: clean fixture has %s expected records, not 6403\n' \
                "$expected_count" >&2
            failed=1
        elif [[ $tree == restricted ]] && (( EUID != 0 )) && \
             (( expected_count >= 6403 )); then
            printf 'FAIL: chmod did not hide any records from the fixture\n' >&2
            failed=1
        fi
        if [[ $tree == clean ]] && \
           [[ -s "$temporary/find-$tree-$algorithm.err" ]]; then
            printf 'FAIL: find() could not traverse clean tree\n' >&2
            failed=1
        fi

        for threads in 1 4; do
            actual="$temporary/actual-$tree-$algorithm-$threads"
            errors="$temporary/errors-$tree-$algorithm-$threads"
            if "$binary" "$directory" "--sha$algorithm" \
                --threads "$threads" --queue-size 32 \
                > "$actual" 2> "$errors"; then
                status=0
            else
                status=$?
            fi

            if ! LC_ALL=C sort -c "$actual" >/dev/null 2>&1; then
                printf 'FAIL: unordered output (%s SHA-%s -j%s)\n' \
                    "$tree" "$algorithm" "$threads" >&2
                failed=1
            fi
            if ! cmp -s "$expected" "$actual"; then
                printf 'FAIL: SHA mismatch/missing/extra entries (%s SHA-%s -j%s)\n' \
                    "$tree" "$algorithm" "$threads" >&2
                diff -u "$expected" "$actual" | head -40 >&2 || true
                failed=1
            fi

            if [[ $tree == clean ]] && (( status != 0 )); then
                printf 'FAIL: clean tree returned %s\n' "$status" >&2
                failed=1
            elif [[ $tree == restricted ]] && (( EUID != 0 )) && \
                 (( status != 1 )); then
                printf 'FAIL: restricted tree returned %s, expected 1\n' \
                    "$status" >&2
                failed=1
            fi
            if [[ $tree == restricted ]] && (( EUID != 0 )) && \
               [[ ! -s $errors ]]; then
                printf 'FAIL: no diagnostics for restricted tree\n' >&2
                failed=1
            fi

            printf 'Tree=%-10s SHA=%-3s threads=%s exit=%s records=%s diagnostics=%s\n' \
                "$tree" "$algorithm" "$threads" "$status" \
                "$(wc -l < "$actual" | tr -d ' ')" \
                "$(wc -l < "$errors" | tr -d ' ')"
        done

        if ! cmp -s \
            "$temporary/actual-$tree-$algorithm-1" \
            "$temporary/actual-$tree-$algorithm-4"; then
            printf 'FAIL: -j1 and -j4 produced different stdout\n' >&2
            failed=1
        fi
    done
done

if (( EUID == 0 )); then
    printf 'WARNING: running as root; chmod-denial assertions skipped.\n' >&2
fi
if (( failed != 0 )); then
    printf 'Verification FAILED.\n' >&2
    exit 1
fi
printf 'Verification PASSED: exact entries, system SHA-2, ordering, thread parity.\n'
