#ifndef MANGEN_MANIFEST_H
#define MANGEN_MANIFEST_H

#include <stddef.h>
#include <stdio.h>

#include "mangen/catalog.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MANGEN_MANIFEST_OK = 0,
    MANGEN_MANIFEST_PARTIAL_ERROR,
    MANGEN_MANIFEST_INVALID_ARGUMENT,
    MANGEN_MANIFEST_INVALID_ROOT,
    MANGEN_MANIFEST_INVALID_PATH,
    MANGEN_MANIFEST_CANCELLED,
    MANGEN_MANIFEST_OUTPUT_ERROR,
    MANGEN_MANIFEST_SYSTEM_ERROR
} mangen_manifest_status_t;

typedef struct {
    size_t records_written;
    size_t files_failed;
} mangen_manifest_report_t;

mangen_manifest_status_t mangen_manifest_write(
    mangen_catalog_t *catalog,
    const char *root_path,
    FILE *output,
    FILE *diagnostics,
    mangen_manifest_report_t *report
);

#ifdef __cplusplus
}
#endif

#endif
