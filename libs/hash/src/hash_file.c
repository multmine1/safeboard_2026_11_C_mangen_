#include "mangen/hash.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <mbedtls/md.h>

#define MANGEN_HASH_READ_BUFFER_SIZE 16384u

static mbedtls_md_type_t mangen_hash_md_type(
    mangen_hash_algorithm_t algorithm
) {
    switch (algorithm) {
        case MANGEN_HASH_SHA256:
            return MBEDTLS_MD_SHA256;

        case MANGEN_HASH_SHA384:
            return MBEDTLS_MD_SHA384;

        case MANGEN_HASH_SHA512:
            return MBEDTLS_MD_SHA512;

        default:
            return MBEDTLS_MD_NONE;
    }
}

mangen_hash_status_t mangen_hash_file(
    const char *path,
    mangen_hash_algorithm_t algorithm,
    mangen_hash_digest_t *out,
    int *error_detail
) {
    if (error_detail != NULL) {
        *error_detail = 0;
    }

    if (out == NULL) {
        return MANGEN_HASH_INVALID_ARGUMENT;
    }

    memset(out, 0, sizeof(*out));

    if (path == NULL || path[0] == '\0') {
        return MANGEN_HASH_INVALID_ARGUMENT;
    }

    const size_t expected_size =
        mangen_hash_digest_size(algorithm);

    if (expected_size == 0u) {
        return MANGEN_HASH_INVALID_ARGUMENT;
    }

    const mbedtls_md_info_t *info =
        mbedtls_md_info_from_type(
            mangen_hash_md_type(algorithm)
        );

    if (info == NULL) {
        if (error_detail != NULL) {
            *error_detail = MBEDTLS_ERR_MD_FEATURE_UNAVAILABLE;
        }

        return MANGEN_HASH_CRYPTO_ERROR;
    }

    if ((size_t)mbedtls_md_get_size(info) != expected_size) {
        return MANGEN_HASH_CRYPTO_ERROR;
    }

    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        if (error_detail != NULL) {
            *error_detail = errno;
        }

        return MANGEN_HASH_OPEN_ERROR;
    }

    mbedtls_md_context_t context;
    mbedtls_md_init(&context);

    mangen_hash_status_t status = MANGEN_HASH_OK;
    int detail = 0;

    int rc = mbedtls_md_setup(&context, info, 0);

    if (rc != 0) {
        status = MANGEN_HASH_CRYPTO_ERROR;
        detail = rc;
        goto cleanup;
    }

    rc = mbedtls_md_starts(&context);

    if (rc != 0) {
        status = MANGEN_HASH_CRYPTO_ERROR;
        detail = rc;
        goto cleanup;
    }

    unsigned char buffer[MANGEN_HASH_READ_BUFFER_SIZE];

    for (;;) {
        const size_t count =
            fread(buffer, 1u, sizeof(buffer), file);

        if (count > 0u) {
            rc = mbedtls_md_update(
                &context,
                buffer,
                count
            );

            if (rc != 0) {
                status = MANGEN_HASH_CRYPTO_ERROR;
                detail = rc;
                goto cleanup;
            }
        }

        if (count < sizeof(buffer)) {
            if (ferror(file)) {
                status = MANGEN_HASH_READ_ERROR;
                detail = errno != 0 ? errno : EIO;
                goto cleanup;
            }

            if (feof(file)) {
                break;
            }

            if (count == 0u) {
                status = MANGEN_HASH_READ_ERROR;
                detail = EIO;
                goto cleanup;
            }
        }
    }

    rc = mbedtls_md_finish(
        &context,
        out->bytes
    );

    if (rc != 0) {
        status = MANGEN_HASH_CRYPTO_ERROR;
        detail = rc;
        goto cleanup;
    }

    out->size = expected_size;

cleanup:
    mbedtls_md_free(&context);

    if (fclose(file) != 0 && status == MANGEN_HASH_OK) {
        status = MANGEN_HASH_READ_ERROR;
        detail = errno != 0 ? errno : EIO;
    }

    if (status != MANGEN_HASH_OK) {
        memset(out, 0, sizeof(*out));
    }

    if (error_detail != NULL) {
        *error_detail = detail;
    }

    return status;
}
