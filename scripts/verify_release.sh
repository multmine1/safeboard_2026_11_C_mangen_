#!/usr/bin/env bash
set -euo pipefail

if (( $# != 2 )); then
    printf 'Usage: %s RELEASE_ZIP TRUSTED_SHA256_TXT\n' "$0" >&2
    exit 2
fi

archive=$1
checksums=$2
[[ -f $archive && -f $checksums ]] || {
    printf 'ZIP or checksum TXT not found.\n' >&2
    exit 2
}

expected_name=$(basename "$archive")
valid_records=0
expected_hash=
while IFS= read -r line || [[ -n $line ]]; do
    [[ -z $line || $line == \#* ]] && continue
    if [[ $line =~ ^([[:xdigit:]]{64})[[:space:]][[:space:]]([^[:space:]]+)$ ]]; then
        hash=${BASH_REMATCH[1]}
        name=${BASH_REMATCH[2]}
        [[ $name == "$expected_name" ]] || {
            printf 'TXT names another archive: %s\n' "$name" >&2
            exit 1
        }
        expected_hash=$hash
        valid_records=$((valid_records + 1))
    else
        printf 'Invalid checksum TXT record: %s\n' "$line" >&2
        exit 1
    fi
done < "$checksums"
(( valid_records == 1 )) || {
    printf 'Expected exactly one checksum record.\n' >&2
    exit 1
}

if command -v shasum >/dev/null 2>&1; then
    output=$(shasum -a 256 "$archive")
elif command -v sha256sum >/dev/null 2>&1; then
    output=$(sha256sum "$archive")
else
    printf 'No SHA-256 checksum tool found.\n' >&2
    exit 2
fi
actual_hash=${output%% *}

actual_upper=$(printf '%s' "$actual_hash" | tr '[:lower:]' '[:upper:]')
expected_upper=$(printf '%s' "$expected_hash" | tr '[:lower:]' '[:upper:]')
if [[ $actual_upper != $expected_upper ]]; then
    printf 'ZIP SHA-256 MISMATCH\nExpected: %s\nActual:   %s\n' \
        "$expected_hash" "$actual_hash" >&2
    exit 1
fi
printf 'ZIP integrity OK: %s\nSHA-256: %s\n' "$expected_name" "$actual_hash"
