#!/usr/bin/env bash
set -euo pipefail

# Self-contained positive and negative OpenSSH signature test.
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/mangen-signature-test.XXXXXXXX")
trap 'rm -r "$temporary"' EXIT

identity='mangen-test@example.org'
ssh-keygen -q -t ed25519 -N '' -f "$temporary/test-key"
printf 'empty.txt : %064d\n' 0 > "$temporary/manifest.txt"
public_key=$(ssh-keygen -y -f "$temporary/test-key")
printf '%s %s\n' "$identity" "$public_key" > "$temporary/allowed-signers"

bash "$root/scripts/sign_manifest.sh" \
    "$temporary/manifest.txt" "$temporary/test-key" >/dev/null
bash "$root/scripts/verify_manifest_signature.sh" \
    "$temporary/manifest.txt" "$temporary/manifest.txt.sig" \
    "$temporary/allowed-signers" "$identity" >/dev/null

cp "$temporary/manifest.txt" "$temporary/tampered.txt"
printf 'changed\n' >> "$temporary/tampered.txt"
if bash "$root/scripts/verify_manifest_signature.sh" \
    "$temporary/tampered.txt" "$temporary/manifest.txt.sig" \
    "$temporary/allowed-signers" "$identity" >/dev/null 2>&1; then
    printf 'FAIL: modified manifest was accepted.\n' >&2
    exit 1
fi

printf 'PASS: authentic manifest accepted; modified manifest rejected.\n'
