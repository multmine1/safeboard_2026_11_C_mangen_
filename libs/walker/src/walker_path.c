#include "walker_internal.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *mangen_walker_join_path(
    const char *parent,
    const char *name
) {
    if (parent == NULL ||
        name == NULL ||
        name[0] == '\0') {
        return NULL;
    }

    const size_t parent_length = strlen(parent);
    const size_t name_length = strlen(name);

    const bool separator_required =
        parent_length > 0u &&
        parent[parent_length - 1u] != '/';

    const size_t separator_size =
        separator_required ? 1u : 0u;

    if (parent_length > SIZE_MAX - separator_size) {
        return NULL;
    }

    const size_t prefix_length =
        parent_length + separator_size;

    if (name_length >= SIZE_MAX - prefix_length) {
        return NULL;
    }

    const size_t result_size =
        prefix_length + name_length + 1u;

    char *result = malloc(result_size);

    if (result == NULL) {
        return NULL;
    }

    memcpy(
        result,
        parent,
        parent_length
    );

    if (separator_required) {
        result[parent_length] = '/';
    }

    memcpy(
        result + prefix_length,
        name,
        name_length
    );

    result[result_size - 1u] = '\0';

    return result;
}

mangen_walker_status_t mangen_walker_is_excluded(
    const mangen_walker_shared_t *shared,
    const char *basename,
    bool *excluded
) {
    if (shared == NULL ||
        shared->options == NULL ||
        basename == NULL ||
        excluded == NULL) {
        return MANGEN_WALKER_INVALID_ARGUMENT;
    }

    *excluded = false;

    const mangen_walker_options_t *options =
        shared->options;

    if (options->exclusion_count == 0u) {
        return MANGEN_WALKER_OK;
    }

    if (options->exclusions == NULL) {
        return MANGEN_WALKER_INVALID_ARGUMENT;
    }

    for (size_t i = 0u;
         i < options->exclusion_count;
         ++i) {

        const mangen_matcher_t *matcher =
            options->exclusions[i];

        if (matcher == NULL) {
            return MANGEN_WALKER_INVALID_ARGUMENT;
        }

        bool matched = false;

        const mangen_matcher_status_t status =
            mangen_matcher_match(
                matcher,
                basename,
                &matched
            );

        if (status != MANGEN_MATCHER_OK) {
            return MANGEN_WALKER_SYSTEM_ERROR;
        }

        if (matched) {
            *excluded = true;
            return MANGEN_WALKER_OK;
        }
    }

    return MANGEN_WALKER_OK;
}

void mangen_walker_report_io_error(
    mangen_walker_worker_t *worker,
    const char *path,
    int error_code
) {
    if (worker == NULL || worker->shared == NULL) {
        return;
    }

    if (worker->report.io_errors < SIZE_MAX) {
        ++worker->report.io_errors;
    }

    if (error_code <= 0) {
        error_code = EIO;
    }

    if (path == NULL) {
        path = "<unknown>";
    }

    mangen_walker_shared_t *shared = worker->shared;

    if (pthread_mutex_lock(&shared->log_mutex) != 0) {
        return;
    }

    fprintf(
        stderr,
        "mangen: walker: %s: %s\n",
        path,
        strerror(error_code)
    );

    pthread_mutex_unlock(&shared->log_mutex);
}
