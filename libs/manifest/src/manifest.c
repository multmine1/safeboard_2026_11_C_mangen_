#include "manifest_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static mangen_manifest_status_t report_file_error(
    FILE *diagnostics,
    const char *relative_path,
    int error_code
) {
    if (diagnostics == NULL) {
        return MANGEN_MANIFEST_OK;
    }

    if (fputs("mangen: cannot hash ", diagnostics) == EOF) {
        return MANGEN_MANIFEST_OUTPUT_ERROR;
    }

    if (mangen_manifest_write_escaped_path(
            diagnostics,
            relative_path
        ) != MANGEN_MANIFEST_OK) {
        return MANGEN_MANIFEST_OUTPUT_ERROR;
    }

    const char *message = error_code > 0
        ? strerror(error_code)
        : "unknown error";

    if (fprintf(diagnostics, ": %s\n", message) < 0) {
        return MANGEN_MANIFEST_OUTPUT_ERROR;
    }

    return MANGEN_MANIFEST_OK;
}

static mangen_manifest_status_t write_record(
    FILE *output,
    const char *relative_path,
    const mangen_catalog_result_t *result
) {
    char hex[MANGEN_MANIFEST_MAX_HEX_SIZE];

    if (result == NULL ||
        result->state != MANGEN_CATALOG_SUCCESS) {
        return MANGEN_MANIFEST_SYSTEM_ERROR;
    }

    if (mangen_manifest_hex(
            result->digest,
            result->digest_size,
            hex,
            sizeof(hex)
        ) != MANGEN_MANIFEST_OK) {
        return MANGEN_MANIFEST_SYSTEM_ERROR;
    }

    if (mangen_manifest_write_escaped_path(
            output,
            relative_path
        ) != MANGEN_MANIFEST_OK) {
        return MANGEN_MANIFEST_OUTPUT_ERROR;
    }

    if (fputs(" : ", output) == EOF ||
        fputs(hex, output) == EOF ||
        fputc('\n', output) == EOF) {
        return MANGEN_MANIFEST_OUTPUT_ERROR;
    }

    return MANGEN_MANIFEST_OK;
}

mangen_manifest_status_t mangen_manifest_write(
    mangen_catalog_t *catalog,
    const char *root_path,
    FILE *output,
    FILE *diagnostics,
    mangen_manifest_report_t *report
) {
    if (report != NULL) {
        memset(report, 0, sizeof(*report));
    }

    if (catalog == NULL ||
        root_path == NULL ||
        root_path[0] == '\0' ||
        output == NULL) {
        return MANGEN_MANIFEST_INVALID_ARGUMENT;
    }

    char *absolute_root = realpath(root_path, NULL);

    if (absolute_root == NULL) {
        return MANGEN_MANIFEST_INVALID_ROOT;
    }

    struct stat root_stat;

    if (stat(absolute_root, &root_stat) != 0 ||
        !S_ISDIR(root_stat.st_mode)) {
        free(absolute_root);
        return MANGEN_MANIFEST_INVALID_ROOT;
    }

    mangen_manifest_report_t local_report = {0};
    mangen_manifest_status_t status = MANGEN_MANIFEST_OK;

    const size_t entry_count = mangen_catalog_size(catalog);

    for (size_t index = 0u; index < entry_count; ++index) {
        mangen_catalog_entry_t *entry =
            mangen_catalog_entry_at(catalog, index);

        if (entry == NULL) {
            status = MANGEN_MANIFEST_SYSTEM_ERROR;
            break;
        }

        const char *absolute_path =
            mangen_catalog_entry_path(entry);

        const char *relative_path = NULL;

        status = mangen_manifest_relative_path(
            absolute_root,
            absolute_path,
            &relative_path
        );

        if (status != MANGEN_MANIFEST_OK) {
            break;
        }

        mangen_catalog_result_t result;

        mangen_catalog_status_t catalog_status =
            mangen_catalog_wait_result(entry, &result);

        if (catalog_status == MANGEN_CATALOG_CANCELLED) {
            status = MANGEN_MANIFEST_CANCELLED;
            break;
        }

        if (catalog_status != MANGEN_CATALOG_OK) {
            status = MANGEN_MANIFEST_SYSTEM_ERROR;
            break;
        }

        if (result.state == MANGEN_CATALOG_ERROR) {
            ++local_report.files_failed;

            status = report_file_error(
                diagnostics,
                relative_path,
                result.error
            );

            if (status != MANGEN_MANIFEST_OK) {
                break;
            }

            continue;
        }

        if (result.state != MANGEN_CATALOG_SUCCESS) {
            status = MANGEN_MANIFEST_SYSTEM_ERROR;
            break;
        }

        status = write_record(
            output,
            relative_path,
            &result
        );

        if (status != MANGEN_MANIFEST_OK) {
            break;
        }

        ++local_report.records_written;
    }

    if (fflush(output) == EOF &&
        (status == MANGEN_MANIFEST_OK ||
         status == MANGEN_MANIFEST_PARTIAL_ERROR)) {
        status = MANGEN_MANIFEST_OUTPUT_ERROR;
    }

    free(absolute_root);

    if (report != NULL) {
        *report = local_report;
    }

    if (status == MANGEN_MANIFEST_OK &&
        local_report.files_failed > 0u) {
        return MANGEN_MANIFEST_PARTIAL_ERROR;
    }

    return status;
}
