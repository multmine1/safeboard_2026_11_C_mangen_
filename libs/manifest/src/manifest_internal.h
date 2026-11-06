#ifndef MANGEN_MANIFEST_INTERNAL_H
#define MANGEN_MANIFEST_INTERNAL_H

#include "mangen/manifest.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define MANGEN_MANIFEST_MAX_DIGEST_SIZE 64u
#define MANGEN_MANIFEST_MAX_HEX_SIZE 129u

mangen_manifest_status_t mangen_manifest_relative_path(
    const char *absolute_root,
    const char *absolute_path,
    const char **relative_path
);

mangen_manifest_status_t mangen_manifest_write_escaped_path(
    FILE *stream,
    const char *path
);

mangen_manifest_status_t mangen_manifest_hex(
    const uint8_t *digest,
    size_t digest_size,
    char *output,
    size_t output_capacity
);

#endif
