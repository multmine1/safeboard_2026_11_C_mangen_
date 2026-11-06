#include "runner.h"

#include "mangen/catalog.h"
#include "mangen/hash_pool.h"
#include "mangen/manifest.h"
#include "mangen/matcher.h"
#include "mangen/walker.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void log_error(
    FILE *diagnostics,
    const char *operation,
    int status
) {
    if (diagnostics != NULL) {
        fprintf(
            diagnostics,
            "mangen: %s failed (status=%d)\n",
            operation,
            status
        );
    }
}

mangen_run_status_t mangen_run(
    const mangen_cli_options_t *options,
    FILE *output,
    FILE *diagnostics,
    mangen_run_report_t *report
) {
    mangen_run_report_t statistics = {0};
    mangen_walker_report_t walker_report = {0};
    mangen_manifest_report_t manifest_report = {0};

    mangen_run_status_t result = MANGEN_RUN_FAILED;

    mangen_catalog_t *catalog = NULL;
    mangen_hash_pool_t *pool = NULL;

    const mangen_matcher_t **exclusions = NULL;
    size_t compiled_count = 0u;

    int pool_started = 0;
    int pool_joined = 0;
    int walker_partial = 0;

    if (report != NULL) {
        memset(report, 0, sizeof(*report));
    }

    if (options == NULL ||
        options->root_path == NULL ||
        output == NULL ||
        options->thread_count == 0u ||
        options->queue_capacity == 0u ||
        (options->exclusion_count != 0u &&
         options->exclusions == NULL)) {
        log_error(diagnostics, "invalid runner arguments", 0);
        return MANGEN_RUN_FAILED;
    }

    /*
     * Compile exclusion patterns.
     */
    if (options->exclusion_count > 0u) {
        if (options->exclusion_count >
            SIZE_MAX / sizeof(*exclusions)) {
            log_error(diagnostics, "exclusion allocation", 0);
            goto cleanup;
        }

        exclusions = calloc(
            options->exclusion_count,
            sizeof(*exclusions)
        );

        if (exclusions == NULL) {
            log_error(diagnostics, "exclusion allocation", 0);
            goto cleanup;
        }
    }

    for (size_t i = 0u; i < options->exclusion_count; ++i) {
        mangen_matcher_t *compiled = NULL;

        const mangen_matcher_status_t status =
            mangen_matcher_compile(
                options->exclusions[i],
                &compiled
            );

        if (status != MANGEN_MATCHER_OK) {
            log_error(
                diagnostics,
                "exclusion pattern compilation",
                (int)status
            );

            goto cleanup;
        }

        exclusions[i] = compiled;
        ++compiled_count;
    }

    /*
     * Create the catalog.
     */
    {
        const mangen_catalog_status_t status =
            mangen_catalog_create(&catalog);

        if (status != MANGEN_CATALOG_OK) {
            log_error(
                diagnostics,
                "catalog creation",
                (int)status
            );

            goto cleanup;
        }
    }

    /*
     * Scan the directory tree.
     *
     * walker finalizes the catalog after successful
     * scanning or partial filesystem errors.
     */
    const mangen_walker_options_t walker_options = {
        .root_path = options->root_path,
        .exclusions = exclusions,
        .exclusion_count = options->exclusion_count,
        .worker_count = options->thread_count,
        .queue_capacity = options->queue_capacity
    };

    const mangen_walker_status_t walker_status =
        mangen_walker_scan(
            &walker_options,
            catalog,
            &walker_report
        );

    statistics.directories_visited =
        walker_report.directories_visited;

    statistics.directories_excluded =
        walker_report.directories_excluded;

    statistics.files_discovered =
        walker_report.files_discovered;

    statistics.files_excluded =
        walker_report.files_excluded;

    statistics.filesystem_errors =
        walker_report.io_errors;

    if (walker_status == MANGEN_WALKER_PARTIAL_IO_ERROR) {
        walker_partial = 1;

        if (diagnostics != NULL) {
            fprintf(
                diagnostics,
                "mangen: directory scan reported "
                "%zu filesystem errors\n",
                walker_report.io_errors
            );
        }
    } else if (walker_status != MANGEN_WALKER_OK) {
        log_error(
            diagnostics,
            "directory scanning",
            (int)walker_status
        );

        goto cleanup;
    }

    /*
     * Create the hashing pool.
     */
    {
        const mangen_hash_pool_status_t status =
            mangen_hash_pool_create(
                catalog,
                options->algorithm,
                options->thread_count,
                options->queue_capacity,
                &pool
            );

        if (status != MANGEN_HASH_POOL_OK) {
            log_error(
                diagnostics,
                "hash pool creation",
                (int)status
            );

            goto cleanup;
        }
    }

    /*
     * Start hashing workers.
     */
    pool_started = 1;

    {
        const mangen_hash_pool_status_t status =
            mangen_hash_pool_start(pool);

        if (status != MANGEN_HASH_POOL_OK) {
            log_error(
                diagnostics,
                "hash pool startup",
                (int)status
            );

            goto cleanup;
        }
    }

    /*
     * Submit every catalog entry exactly once.
     */
    const size_t entry_count =
        mangen_catalog_size(catalog);

    for (size_t index = 0u; index < entry_count; ++index) {
        const mangen_hash_pool_status_t status =
            mangen_hash_pool_submit(pool, index);

        if (status != MANGEN_HASH_POOL_OK) {
            log_error(
                diagnostics,
                "hash job submission",
                (int)status
            );

            goto cleanup;
        }
    }

    /*
     * No further jobs will be submitted.
     *
     * Workers may continue hashing queued files.
     */
    {
        const mangen_hash_pool_status_t status =
            mangen_hash_pool_close(pool);

        if (status != MANGEN_HASH_POOL_OK) {
            log_error(
                diagnostics,
                "hash pool closing",
                (int)status
            );

            goto cleanup;
        }
    }

    /*
     * Write the manifest in catalog order.
     *
     * manifest waits for pending entries using
     * the catalog condition variable.
     */
    const mangen_manifest_status_t manifest_status =
        mangen_manifest_write(
            catalog,
            options->root_path,
            output,
            diagnostics,
            &manifest_report
        );

    if (manifest_status == MANGEN_MANIFEST_CANCELLED) {
        result = MANGEN_RUN_CANCELLED;
        goto cleanup;
    }

    if (manifest_status != MANGEN_MANIFEST_OK &&
        manifest_status != MANGEN_MANIFEST_PARTIAL_ERROR) {
        log_error(
            diagnostics,
            "manifest output",
            (int)manifest_status
        );

        goto cleanup;
    }

    /*
     * Wait for all hashing workers.
     */
    {
        const mangen_hash_pool_status_t status =
            mangen_hash_pool_join(pool);

        pool_joined = 1;

        if (status != MANGEN_HASH_POOL_OK) {
            log_error(
                diagnostics,
                "hash worker joining",
                (int)status
            );

            goto cleanup;
        }
    }

    statistics.hashes_completed =
        mangen_hash_pool_completed(pool);

    statistics.hashes_failed =
        mangen_hash_pool_failed(pool);

    if (walker_partial ||
        manifest_status == MANGEN_MANIFEST_PARTIAL_ERROR ||
        statistics.hashes_failed > 0u) {
        result = MANGEN_RUN_PARTIAL_ERROR;
    } else {
        result = MANGEN_RUN_OK;
    }

cleanup:
    /*
     * Stop workers before releasing catalog entries.
     */
    if (pool != NULL) {
        if (pool_started && !pool_joined) {
            mangen_hash_pool_cancel(pool);
            (void)mangen_hash_pool_join(pool);
        }

        statistics.hashes_completed =
            mangen_hash_pool_completed(pool);

        statistics.hashes_failed =
            mangen_hash_pool_failed(pool);

        mangen_hash_pool_destroy(pool);
    }

    if (catalog != NULL) {
        mangen_catalog_destroy(catalog);
    }

    for (size_t i = 0u; i < compiled_count; ++i) {
        mangen_matcher_free(
            (mangen_matcher_t *)exclusions[i]
        );
    }

    free(exclusions);

    if (report != NULL) {
        *report = statistics;
    }

    return result;
}
