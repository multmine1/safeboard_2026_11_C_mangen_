#!/usr/bin/env bash
set -euo pipefail

if (( $# != 2 )); then
    printf 'Usage: %s MANIFEST SSH_PRIVATE_KEY\n' "$0" >&2
    exit 2
fi

manifest=$1
signing_key=$2
[[ -f $manifest && -f $signing_key ]] || {
    printf 'Manifest or signing key missing.\n' >&2
    exit 2
}
[[ ! -e "$manifest.sig" ]] || {
    printf 'Signature already exists; move it aside explicitly: %s.sig\n' \
        "$manifest" >&2
    exit 2
}

ssh-keygen -Y sign -f "$signing_key" -n mangen-manifest "$manifest"
[[ -s "$manifest.sig" ]] || { printf 'Signature was not created.\n' >&2; exit 1; }
printf 'Signature created: %s.sig\n' "$manifest"
