#ifndef MANGEN_WALKER_H
#define MANGEN_WALKER_H

#include <stddef.h>

#include "mangen/catalog.h"
#include "mangen/matcher.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MANGEN_WALKER_OK = 0,
    MANGEN_WALKER_PARTIAL_IO_ERROR,
    MANGEN_WALKER_INVALID_ARGUMENT,
    MANGEN_WALKER_INVALID_ROOT,
    MANGEN_WALKER_OUT_OF_MEMORY,
    MANGEN_WALKER_LIMIT_EXCEEDED,
    MANGEN_WALKER_QUEUE_FULL,
    MANGEN_WALKER_CANCELLED,
    MANGEN_WALKER_SYSTEM_ERROR
} mangen_walker_status_t;

typedef struct {
    const char *root_path;

    const mangen_matcher_t *const *exclusions;
    size_t exclusion_count;

    size_t worker_count;
    size_t queue_capacity;
} mangen_walker_options_t;

typedef struct {
    size_t directories_visited;
    size_t files_discovered;

    size_t files_excluded;
    size_t directories_excluded;

    size_t io_errors;
} mangen_walker_report_t;

mangen_walker_status_t mangen_walker_scan(
    const mangen_walker_options_t *options,
    mangen_catalog_t *catalog,
    mangen_walker_report_t *report
);

#ifdef __cplusplus
}
#endif

#endif
