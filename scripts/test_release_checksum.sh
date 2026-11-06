#!/usr/bin/env bash
set -euo pipefail

# Positive/negative checksum verification using a disposable ZIP.
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/mangen-checksum-test.XXXXXXXX")
trap 'rm -r "$temporary"' EXIT
command -v zip >/dev/null 2>&1 || { printf 'zip missing\n' >&2; exit 2; }

(cd "$root" && zip -q "$temporary/test.zip" README.md)
if command -v shasum >/dev/null 2>&1; then
    output=$(shasum -a 256 "$temporary/test.zip")
elif command -v sha256sum >/dev/null 2>&1; then
    output=$(sha256sum "$temporary/test.zip")
else
    printf 'No system SHA-256 command found\n' >&2
    exit 2
fi
digest=${output%% *}
printf '# Source commit A: local smoke test\n%s  test.zip\n' \
    "$digest" > "$temporary/test.sha256.txt"

bash "$root/scripts/verify_release.sh" \
    "$temporary/test.zip" "$temporary/test.sha256.txt" >/dev/null

mkdir -p "$temporary/tampered"
cp "$temporary/test.zip" "$temporary/tampered/test.zip"
printf 'corruption\n' >> "$temporary/tampered/test.zip"
if bash "$root/scripts/verify_release.sh" \
    "$temporary/tampered/test.zip" "$temporary/test.sha256.txt" \
    >/dev/null 2>&1; then
    printf 'FAIL: corrupted ZIP was accepted.\n' >&2
    exit 1
fi
printf 'PASS: original ZIP accepted; corrupted ZIP rejected.\n'
