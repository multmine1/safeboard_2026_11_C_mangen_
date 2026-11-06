#!/usr/bin/env bash
set -euo pipefail

if (( $# != 4 )); then
    printf 'Usage: %s MANIFEST SIGNATURE TRUSTED_ALLOWED_SIGNERS SIGNER_IDENTITY\n' \
        "$0" >&2
    exit 2
fi

manifest=$1
signature=$2
allowed_signers=$3
identity=$4

[[ -f $manifest && -f $signature && -f $allowed_signers ]] || {
    printf 'Manifest, signature or trusted signers file missing.\n' >&2
    exit 2
}

ssh-keygen -Y verify -f "$allowed_signers" -I "$identity" \
    -n mangen-manifest -s "$signature" < "$manifest"
printf 'Manifest signature is authentic for trusted signer: %s\n' "$identity"
