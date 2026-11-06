#!/usr/bin/env bash
set -euo pipefail

# Bash 3.2+; macOS and Linux. Writes the absolute fixture root to stdout.
umask 077
base=$(mktemp -d "${TMPDIR:-/tmp}/mangen-qa.XXXXXXXX")
clean="$base/clean"
restricted="$base/restricted"
mkdir -p "$clean" "$restricted"
printf 'mangen-test-fixtures-v1\n' > "$base/.mangen-fixture"

for layer in {1..5}; do
    for branch in {1..8}; do
        for leaf in {1..8}; do
            directory="$clean/layer-$layer/branch-$branch/leaf-$leaf"
            mkdir -p "$directory"
            for index in {1..20}; do
                printf 'layer=%s branch=%s leaf=%s file=%s\n' \
                    "$layer" "$branch" "$leaf" "$index" \
                    > "$directory/file-$index.txt"
            done
        done
    done
done

: > "$clean/empty.txt"
mkdir -p "$clean/empty-directory"
ln -s empty.txt "$clean/linked-empty.txt"
dd if=/dev/zero of="$clean/large.bin" bs=1048576 count=16 2>/dev/null

cp -R "$clean/." "$restricted/"

# Deliberately randomize file and directory modes, not ownership or ACLs.
# Directory modes are changed bottom-up so every child is handled first.
while IFS= read -r file; do
    case $((RANDOM % 5)) in
        0) mode=000 ;;
        1) mode=0400 ;;
        2) mode=0600 ;;
        3) mode=0640 ;;
        4) mode=0644 ;;
    esac
    chmod "$mode" "$file"
done < <(find "$restricted" -type f -print)

while IFS= read -r directory; do
    case $((RANDOM % 5)) in
        0) mode=000 ;;
        1) mode=0500 ;;
        2) mode=0700 ;;
        3) mode=0750 ;;
        4) mode=0755 ;;
    esac
    chmod "$mode" "$directory"
done < <(find "$restricted" -depth -type d ! -path "$restricted" -print)

# At least one guaranteed inaccessible file and directory for non-root runs.
chmod 000 "$restricted/empty.txt" "$restricted/layer-1"

printf 'Created two trees: %s\n' "$base" >&2
printf 'Clean:      %s\n' "$clean" >&2
printf 'Restricted: %s\n' "$restricted" >&2
printf 'Expected clean records: 6403 (6400 text, 16 MiB, empty, symlink)\n' >&2
printf '%s\n' "$base"
