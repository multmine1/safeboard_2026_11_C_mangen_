#include "walker_internal.h"

#include <pthread.h>

void *mangen_walker_thread_main(void *argument) {
    mangen_walker_worker_t *worker = argument;

    if (worker == NULL || worker->shared == NULL) {
        return NULL;
    }

    mangen_walker_shared_t *shared = worker->shared;

    for (;;) {
        if (pthread_mutex_lock(&shared->mutex) != 0) {
            mangen_walker_abort(
                shared,
                MANGEN_WALKER_SYSTEM_ERROR
            );

            return NULL;
        }

        mangen_walker_dir_job_t *job = NULL;

        for (;;) {
            if (shared->aborted) {
                pthread_mutex_unlock(&shared->mutex);
                return NULL;
            }

            void *item = NULL;

            const mangen_queue_status_t status =
                mangen_queue_try_pop(
                    shared->queue,
                    &item
                );

            if (status == MANGEN_QUEUE_OK) {
                job = item;
                break;
            }

            if (status == MANGEN_QUEUE_CLOSED) {
                pthread_mutex_unlock(&shared->mutex);
                return NULL;
            }

            if (status == MANGEN_QUEUE_ABORTED) {
                mangen_walker_abort_locked(
                    shared,
                    MANGEN_WALKER_CANCELLED
                );

                pthread_mutex_unlock(&shared->mutex);
                return NULL;
            }

            if (status != MANGEN_QUEUE_EMPTY) {
                mangen_walker_abort_locked(
                    shared,
                    MANGEN_WALKER_SYSTEM_ERROR
                );

                pthread_mutex_unlock(&shared->mutex);
                return NULL;
            }

            if (shared->completed) {
                pthread_mutex_unlock(&shared->mutex);
                return NULL;
            }

            /*
             * Queue is open but contains no pending job.
             * Wait until a new directory is enqueued or
             * traversal completes.
             */
            const int rc = pthread_cond_wait(
                &shared->work_ready,
                &shared->mutex
            );

            if (rc != 0) {
                mangen_walker_abort_locked(
                    shared,
                    MANGEN_WALKER_SYSTEM_ERROR
                );

                pthread_mutex_unlock(&shared->mutex);
                return NULL;
            }
        }

        pthread_mutex_unlock(&shared->mutex);

        if (job == NULL) {
            mangen_walker_abort(
                shared,
                MANGEN_WALKER_SYSTEM_ERROR
            );

            return NULL;
        }

        /*
         * Process directory without holding
         * the shared traversal mutex.
         */
        const mangen_walker_status_t result =
            mangen_walker_process_directory(
                worker,
                job
            );

        /*
         * The worker owns the job after successful pop.
         */
        mangen_walker_job_dispose(job, NULL);

        if (result != MANGEN_WALKER_OK) {
            mangen_walker_abort(shared, result);
            return NULL;
        }

        /*
         * One directory job has been completed.
         * This may close the queue if pending_jobs == 0.
         */
        mangen_walker_directory_finished(shared);
    }
}
