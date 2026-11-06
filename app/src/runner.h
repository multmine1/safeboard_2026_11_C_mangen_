#ifndef MANGEN_RUNNER_H
#define MANGEN_RUNNER_H

#include <stddef.h>
#include <stdio.h>

#include "cli.h"

typedef enum {
    MANGEN_RUN_OK = 0,
    MANGEN_RUN_PARTIAL_ERROR,
    MANGEN_RUN_FAILED,
    MANGEN_RUN_CANCELLED
} mangen_run_status_t;

typedef struct {
    size_t directories_visited;
    size_t directories_excluded;

    size_t files_discovered;
    size_t files_excluded;

    size_t hashes_completed;
    size_t hashes_failed;

    size_t filesystem_errors;
} mangen_run_report_t;

/*
 * Executes the complete processing pipeline:
 *
 * 1. Compile exclusion patterns.
 * 2. Scan the directory tree.
 * 3. Finalize the sorted catalog.
 * 4. Hash files using worker threads.
 * 5. Write the ordered manifest.
 *
 * options is borrowed and remains unchanged.
 *
 * output receives manifest records.
 * diagnostics receives error messages.
 *
 * report may be NULL.
 *
 * The runner owns and releases all internal resources.
 */
mangen_run_status_t mangen_run(
    const mangen_cli_options_t *options,
    FILE *output,
    FILE *diagnostics,
    mangen_run_report_t *report
);

#endif
