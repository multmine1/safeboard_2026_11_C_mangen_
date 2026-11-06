#!/usr/bin/env bash
set -euo pipefail

# Run only after committing sources, README and generated PDFs (commit A).
# Produces a ZIP and checksum TXT together in safeboard/ for commit B.
if (( $# != 0 )); then
    printf 'Usage: %s\n' "$0" >&2
    exit 2
fi

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

if [[ -n $(git status --porcelain --untracked-files=all) ]]; then
    printf 'Working tree must be clean: commit A before packaging.\n' >&2
    exit 1
fi
for source in app/src/main.c libs/walker/CMakeLists.txt \
              libs/manifest/CMakeLists.txt README.md \
              docs/PDF/architecture.pdf docs/PDF/user-guide.pdf \
              docs/PDF/verification.pdf; do
    if [[ ! -f $source ]]; then
        printf 'Required release component missing: %s\n' "$source" >&2
        exit 1
    fi
done

if git ls-files --stage | awk '$1 == "160000" { found=1 } END { exit !found }'; then
    printf 'Git submodules detected; git archive would omit their contents.\n' >&2
    exit 1
fi

for program in git tar zip; do
    command -v "$program" >/dev/null 2>&1 || {
        printf 'Missing packaging tool: %s\n' "$program" >&2
        exit 2
    }
done

commit=$(git rev-parse HEAD)
short=$(git rev-parse --short=12 HEAD)
archive_name="mangen-$short.zip"
checksum_file="safeboard/mangen-$short.sha256.txt"
archive="$root/safeboard/$archive_name"

[[ ! -e $archive && ! -e $checksum_file ]] || {
    printf 'Release files already exist; refusing to overwrite.\n' >&2
    exit 1
}

staging=$(mktemp -d "${TMPDIR:-/tmp}/mangen-release.XXXXXXXX")
trap 'rm -r "$staging"' EXIT
mkdir -p "$staging/mangen" "$root/safeboard"

git archive --format=tar "$commit" . ':(exclude)safeboard' | \
    tar -xf - -C "$staging/mangen"

# A commit cannot contain its own hash: the ZIP adds this generated file.
# When compiling the ZIP outside Git, CMake can embed this exact source ID.
printf '%s\n' "$commit" > "$staging/mangen/VERSION-COMMIT"

(cd "$staging" && zip -qr "$archive" mangen)
if command -v unzip >/dev/null 2>&1; then
    unzip -tq "$archive" >/dev/null
fi

if command -v shasum >/dev/null 2>&1; then
    digest=$(shasum -a 256 "$archive")
elif command -v sha256sum >/dev/null 2>&1; then
    digest=$(sha256sum "$archive")
else
    printf 'No SHA-256 checksum command found.\n' >&2
    exit 2
fi
digest=${digest%% *}

printf '# Source commit A: %s\n%s  %s\n' \
    "$commit" "$digest" "$archive_name" > "$checksum_file"

printf 'ZIP: %s\n' "$archive"
printf 'SHA-256: %s\n' "$digest"
printf 'Checksum TXT (to commit B): %s\n' "$root/$checksum_file"
printf 'Next: git add "%s" "%s" && git commit\n' \
    "safeboard/$archive_name" "$checksum_file"
