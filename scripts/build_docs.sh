#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
command -v latexmk >/dev/null 2>&1 || {
    printf 'latexmk not found; install a TeX distribution.\n' >&2
    exit 2
}
command -v xelatex >/dev/null 2>&1 || {
    printf 'xelatex not found; install the XeLaTeX engine.\n' >&2
    exit 2
}

mkdir -p "$root/docs/PDF"
for document in architecture user-guide verification; do
    source_dir="$root/docs/latex/$document"
    build_dir="$root/build/docs/$document"
    mkdir -p "$build_dir"
    printf 'Building %s.pdf...\n' "$document"
    (cd "$source_dir" && latexmk -silent -pdfxe -interaction=nonstopmode \
        -halt-on-error -file-line-error \
        -outdir="$build_dir" main.tex)
    cp "$build_dir/main.pdf" "$root/docs/PDF/$document.pdf"
done
printf 'Documentation PDFs ready in %s\n' "$root/docs/PDF"
