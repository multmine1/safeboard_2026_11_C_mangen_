#include "walker_internal.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static mangen_walker_status_t map_catalog_status(
    mangen_catalog_status_t status
) {
    switch (status) {
        case MANGEN_CATALOG_OK:
            return MANGEN_WALKER_OK;

        case MANGEN_CATALOG_OUT_OF_MEMORY:
            return MANGEN_WALKER_OUT_OF_MEMORY;

        case MANGEN_CATALOG_LIMIT_EXCEEDED:
            return MANGEN_WALKER_LIMIT_EXCEEDED;

        default:
            return MANGEN_WALKER_SYSTEM_ERROR;
    }
}

static mangen_walker_status_t process_entry(
    mangen_walker_worker_t *worker,
    const mangen_walker_dir_job_t *parent,
    const char *basename
) {
    mangen_walker_shared_t *shared = worker->shared;

    char *absolute_path = mangen_walker_join_path(
        parent->absolute_path,
        basename
    );

    if (absolute_path == NULL) {
        return MANGEN_WALKER_OUT_OF_MEMORY;
    }

    struct stat metadata;

    if (lstat(absolute_path, &metadata) != 0) {
        const int error_code = errno;

        if (error_code == ENOMEM) {
            free(absolute_path);
            return MANGEN_WALKER_OUT_OF_MEMORY;
        }

        mangen_walker_report_io_error(
            worker,
            absolute_path,
            error_code
        );

        free(absolute_path);
        return MANGEN_WALKER_OK;
    }

    bool excluded = false;

    mangen_walker_status_t status =
        mangen_walker_is_excluded(
            shared,
            basename,
            &excluded
        );

    if (status != MANGEN_WALKER_OK) {
        free(absolute_path);
        return status;
    }

    if (excluded) {
        if (S_ISDIR(metadata.st_mode)) {
            ++worker->report.directories_excluded;
        } else {
            ++worker->report.files_excluded;
        }

        free(absolute_path);
        return MANGEN_WALKER_OK;
    }

    bool is_directory = S_ISDIR(metadata.st_mode);
    bool is_regular_file = S_ISREG(metadata.st_mode);

    /*
     * Include symbolic links to regular files.
     * Do not traverse symbolic links to directories.
     */
    if (S_ISLNK(metadata.st_mode)) {
        struct stat target;

        if (stat(absolute_path, &target) != 0) {
            const int error_code = errno;

            if (error_code == ENOMEM) {
                free(absolute_path);
                return MANGEN_WALKER_OUT_OF_MEMORY;
            }

            mangen_walker_report_io_error(
                worker,
                absolute_path,
                error_code
            );

            free(absolute_path);
            return MANGEN_WALKER_OK;
        }

        is_directory = false;
        is_regular_file = S_ISREG(target.st_mode);
    }

    if (is_directory) {
        char *relative_path = mangen_walker_join_path(
            parent->relative_path,
            basename
        );

        if (relative_path == NULL) {
            free(absolute_path);
            return MANGEN_WALKER_OUT_OF_MEMORY;
        }

        mangen_walker_dir_job_t *job =
            mangen_walker_job_create(
                absolute_path,
                relative_path
            );

        free(relative_path);
        free(absolute_path);

        if (job == NULL) {
            return MANGEN_WALKER_OUT_OF_MEMORY;
        }

        status = mangen_walker_enqueue_directory(
            shared,
            job
        );

        if (status != MANGEN_WALKER_OK) {
            /*
             * Failed enqueue: ownership remains with caller.
             */
            mangen_walker_job_dispose(job, NULL);
        }

        return status;
    }

    if (is_regular_file) {
        const mangen_catalog_status_t catalog_status =
            mangen_catalog_batch_append(
                worker->batch,
                absolute_path
            );

        if (catalog_status != MANGEN_CATALOG_OK) {
            free(absolute_path);
            return map_catalog_status(catalog_status);
        }

        /*
         * Ownership of absolute_path transferred to batch.
         */
        ++worker->report.files_discovered;

        return MANGEN_WALKER_OK;
    }

    /*
     * Ignore sockets, devices, FIFOs and other special files.
     */
    free(absolute_path);

    return MANGEN_WALKER_OK;
}

mangen_walker_status_t mangen_walker_process_directory(
    mangen_walker_worker_t *worker,
    const mangen_walker_dir_job_t *job
) {
    if (worker == NULL ||
        worker->shared == NULL ||
        worker->batch == NULL ||
        job == NULL ||
        job->absolute_path == NULL ||
        job->relative_path == NULL) {
        return MANGEN_WALKER_INVALID_ARGUMENT;
    }

    DIR *directory = opendir(job->absolute_path);

    if (directory == NULL) {
        const int error_code = errno;

        if (error_code == ENOMEM) {
            return MANGEN_WALKER_OUT_OF_MEMORY;
        }

        mangen_walker_report_io_error(
            worker,
            job->absolute_path,
            error_code
        );

        return MANGEN_WALKER_OK;
    }

    ++worker->report.directories_visited;

    mangen_walker_status_t result = MANGEN_WALKER_OK;

    for (;;) {
        errno = 0;

        struct dirent *entry = readdir(directory);

        if (entry == NULL) {
            if (errno != 0) {
                const int error_code = errno;

                if (error_code == ENOMEM) {
                    result = MANGEN_WALKER_OUT_OF_MEMORY;
                } else {
                    mangen_walker_report_io_error(
                        worker,
                        job->absolute_path,
                        error_code
                    );
                }
            }

            break;
        }

        const char *name = entry->d_name;

        if (strcmp(name, ".") == 0 ||
            strcmp(name, "..") == 0) {
            continue;
        }

        result = process_entry(
            worker,
            job,
            name
        );

        if (result != MANGEN_WALKER_OK) {
            break;
        }
    }

    if (closedir(directory) != 0) {
        mangen_walker_report_io_error(
            worker,
            job->absolute_path,
            errno
        );
    }

    return result;
}
