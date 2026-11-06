#include "manifest_internal.h"

#include <stdio.h>
#include <string.h>

mangen_manifest_status_t mangen_manifest_relative_path(
    const char *absolute_root,
    const char *absolute_path,
    const char **relative_path
) {
    if (relative_path == NULL || absolute_path == NULL) {
        return MANGEN_MANIFEST_INVALID_ARGUMENT;
    }

    *relative_path = NULL;

    if (absolute_root == NULL ||
        absolute_root[0] != '/') {
        return MANGEN_MANIFEST_INVALID_ROOT;
    }

    if (absolute_path[0] != '/') {
        return MANGEN_MANIFEST_INVALID_PATH;
    }

    size_t root_length = strlen(absolute_root);

    /*
     * Ignore trailing separators, except for root "/".
     */
    while (root_length > 1u &&
           absolute_root[root_length - 1u] == '/') {
        --root_length;
    }

    if (root_length == 1u && absolute_root[0] == '/') {
        if (absolute_path[1] == '\0') {
            return MANGEN_MANIFEST_INVALID_PATH;
        }

        *relative_path = absolute_path + 1u;
        return MANGEN_MANIFEST_OK;
    }

    if (strncmp(
            absolute_root,
            absolute_path,
            root_length
        ) != 0) {
        return MANGEN_MANIFEST_INVALID_PATH;
    }

    /*
     * Require a directory boundary.
     *
     * /root/data/file.txt -> valid for /root/data
     * /root/data2/file.txt -> invalid for /root/data
     */
    if (absolute_path[root_length] != '/') {
        return MANGEN_MANIFEST_INVALID_PATH;
    }

    const char *relative =
        absolute_path + root_length + 1u;

    if (relative[0] == '\0') {
        return MANGEN_MANIFEST_INVALID_PATH;
    }

    *relative_path = relative;

    return MANGEN_MANIFEST_OK;
}

mangen_manifest_status_t mangen_manifest_write_escaped_path(
    FILE *stream,
    const char *path
) {
    if (stream == NULL || path == NULL) {
        return MANGEN_MANIFEST_INVALID_ARGUMENT;
    }

    const unsigned char *cursor =
        (const unsigned char *)path;

    while (*cursor != '\0') {
        const char *escape = NULL;

        switch (*cursor) {
            case '\\':
                escape = "\\\\";
                break;

            case '\n':
                escape = "\\n";
                break;

            case '\r':
                escape = "\\r";
                break;

            default:
                break;
        }

        if (escape != NULL) {
            if (fputs(escape, stream) == EOF) {
                return MANGEN_MANIFEST_OUTPUT_ERROR;
            }
        } else {
            if (fputc(*cursor, stream) == EOF) {
                return MANGEN_MANIFEST_OUTPUT_ERROR;
            }
        }

        ++cursor;
    }

    return MANGEN_MANIFEST_OK;
}
