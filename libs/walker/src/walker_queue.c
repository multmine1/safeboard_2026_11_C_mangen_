#include "walker_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

mangen_walker_dir_job_t *mangen_walker_job_create(
    const char *absolute_path,
    const char *relative_path
) {
    if (absolute_path == NULL || relative_path == NULL) {
        return NULL;
    }

    const size_t absolute_length = strlen(absolute_path);
    const size_t relative_length = strlen(relative_path);

    if (absolute_length == SIZE_MAX ||
        relative_length == SIZE_MAX) {
        return NULL;
    }

    const size_t absolute_size = absolute_length + 1u;
    const size_t relative_size = relative_length + 1u;

    if (absolute_size > SIZE_MAX - relative_size) {
        return NULL;
    }

    const size_t strings_size =
        absolute_size + relative_size;

    if (strings_size >
        SIZE_MAX - sizeof(mangen_walker_dir_job_t)) {
        return NULL;
    }

    mangen_walker_dir_job_t *job = malloc(
        sizeof(*job) + strings_size
    );

    if (job == NULL) {
        return NULL;
    }

    char *storage = (char *)(job + 1);

    job->absolute_path = storage;
    job->relative_path = storage + absolute_size;

    memcpy(
        job->absolute_path,
        absolute_path,
        absolute_size
    );

    memcpy(
        job->relative_path,
        relative_path,
        relative_size
    );

    return job;
}

void mangen_walker_job_dispose(
    void *item,
    void *context
) {
    (void)context;
    free(item);
}

void mangen_walker_abort_locked(
    mangen_walker_shared_t *shared,
    mangen_walker_status_t reason
) {
    if (shared->aborted) {
        return;
    }

    if (reason == MANGEN_WALKER_OK) {
        reason = MANGEN_WALKER_SYSTEM_ERROR;
    }

    shared->aborted = true;
    shared->fatal_status = reason;

    mangen_queue_abort(shared->queue);

    pthread_cond_broadcast(&shared->work_ready);
}

void mangen_walker_abort(
    mangen_walker_shared_t *shared,
    mangen_walker_status_t reason
) {
    if (shared == NULL) {
        return;
    }

    if (pthread_mutex_lock(&shared->mutex) != 0) {
        return;
    }

    mangen_walker_abort_locked(shared, reason);

    pthread_mutex_unlock(&shared->mutex);
}

static mangen_walker_status_t map_queue_error(
    mangen_queue_status_t status
) {
    switch (status) {
        case MANGEN_QUEUE_FULL:
            return MANGEN_WALKER_QUEUE_FULL;

        case MANGEN_QUEUE_OUT_OF_MEMORY:
            return MANGEN_WALKER_OUT_OF_MEMORY;

        case MANGEN_QUEUE_LIMIT_EXCEEDED:
            return MANGEN_WALKER_LIMIT_EXCEEDED;

        case MANGEN_QUEUE_CLOSED:
        case MANGEN_QUEUE_ABORTED:
            return MANGEN_WALKER_CANCELLED;

        default:
            return MANGEN_WALKER_SYSTEM_ERROR;
    }
}

mangen_walker_status_t mangen_walker_enqueue_directory(
    mangen_walker_shared_t *shared,
    mangen_walker_dir_job_t *job
) {
    if (shared == NULL || job == NULL) {
        return MANGEN_WALKER_INVALID_ARGUMENT;
    }

    if (pthread_mutex_lock(&shared->mutex) != 0) {
        return MANGEN_WALKER_SYSTEM_ERROR;
    }

    if (shared->aborted) {
        const mangen_walker_status_t result =
            shared->fatal_status;

        pthread_mutex_unlock(&shared->mutex);
        return result;
    }

    if (shared->completed) {
        pthread_mutex_unlock(&shared->mutex);
        return MANGEN_WALKER_CANCELLED;
    }

    if (shared->pending_jobs == SIZE_MAX) {
        mangen_walker_abort_locked(
            shared,
            MANGEN_WALKER_LIMIT_EXCEEDED
        );

        pthread_mutex_unlock(&shared->mutex);

        return MANGEN_WALKER_LIMIT_EXCEEDED;
    }

    const mangen_queue_status_t queue_status =
        mangen_queue_try_push(
            shared->queue,
            job
        );

    if (queue_status != MANGEN_QUEUE_OK) {
        const mangen_walker_status_t result =
            map_queue_error(queue_status);

        mangen_walker_abort_locked(shared, result);

        pthread_mutex_unlock(&shared->mutex);

        return result;
    }

    /*
     * The worker cannot pop this job until mutex is released.
     */
    ++shared->pending_jobs;

    pthread_cond_signal(&shared->work_ready);

    pthread_mutex_unlock(&shared->mutex);

    return MANGEN_WALKER_OK;
}

void mangen_walker_directory_finished(
    mangen_walker_shared_t *shared
) {
    if (shared == NULL) {
        return;
    }

    if (pthread_mutex_lock(&shared->mutex) != 0) {
        return;
    }

    if (shared->aborted) {
        pthread_mutex_unlock(&shared->mutex);
        return;
    }

    if (shared->pending_jobs == 0u) {
        mangen_walker_abort_locked(
            shared,
            MANGEN_WALKER_SYSTEM_ERROR
        );

        pthread_mutex_unlock(&shared->mutex);
        return;
    }

    --shared->pending_jobs;

    if (shared->pending_jobs == 0u) {
        shared->completed = true;

        const mangen_queue_status_t status =
            mangen_queue_close(shared->queue);

        if (status != MANGEN_QUEUE_OK) {
            mangen_walker_abort_locked(
                shared,
                MANGEN_WALKER_SYSTEM_ERROR
            );
        }

        pthread_cond_broadcast(&shared->work_ready);
    }

    pthread_mutex_unlock(&shared->mutex);
}
