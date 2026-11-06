#ifndef MANGEN_WALKER_INTERNAL_H
#define MANGEN_WALKER_INTERNAL_H

#include "mangen/walker.h"
#include "mangen/queue.h"

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct mangen_walker_shared mangen_walker_shared_t;

typedef struct {
    char *absolute_path;
    char *relative_path;
} mangen_walker_dir_job_t;

typedef struct {
    mangen_walker_shared_t *shared;

    pthread_t thread;

    mangen_catalog_batch_t *batch;
    mangen_walker_report_t report;
} mangen_walker_worker_t;

struct mangen_walker_shared {
    const mangen_walker_options_t *options;

    mangen_catalog_t *catalog;
    mangen_queue_t *queue;

    mangen_walker_worker_t *workers;
    size_t worker_count;
    size_t threads_started;

    size_t pending_jobs;

    pthread_mutex_t mutex;
    pthread_mutex_t log_mutex;
    pthread_cond_t work_ready;

    bool completed;
    bool aborted;

    mangen_walker_status_t fatal_status;
};

/*
 * Directory job allocation and ownership.
 */

mangen_walker_dir_job_t *mangen_walker_job_create(
    const char *absolute_path,
    const char *relative_path
);

void mangen_walker_job_dispose(
    void *item,
    void *context
);

/*
 * Shared queue management.
 *
 * On successful enqueue, ownership transfers to the queue.
 * On failure, ownership remains with the caller.
 */

mangen_walker_status_t mangen_walker_enqueue_directory(
    mangen_walker_shared_t *shared,
    mangen_walker_dir_job_t *job
);

void mangen_walker_directory_finished(
    mangen_walker_shared_t *shared
);

/*
 * Cancellation.
 */

void mangen_walker_abort(
    mangen_walker_shared_t *shared,
    mangen_walker_status_t reason
);

/*
 * Requires shared->mutex to be locked.
 */

void mangen_walker_abort_locked(
    mangen_walker_shared_t *shared,
    mangen_walker_status_t reason
);

/*
 * Filesystem processing.
 */

void *mangen_walker_thread_main(
    void *argument
);

mangen_walker_status_t mangen_walker_process_directory(
    mangen_walker_worker_t *worker,
    const mangen_walker_dir_job_t *job
);

/*
 * Path operations.
 *
 * Returned strings are allocated and owned by the caller.
 */

char *mangen_walker_join_path(
    const char *parent,
    const char *name
);

/*
 * Exclusion matching.
 */

mangen_walker_status_t mangen_walker_is_excluded(
    const mangen_walker_shared_t *shared,
    const char *basename,
    bool *excluded
);

/*
 * Error reporting.
 */

void mangen_walker_report_io_error(
    mangen_walker_worker_t *worker,
    const char *path,
    int error_code
);

#endif
