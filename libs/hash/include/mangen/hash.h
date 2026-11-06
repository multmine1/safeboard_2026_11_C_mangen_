#ifndef MANGEN_HASH_H
#define MANGEN_HASH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MANGEN_HASH_MAX_DIGEST_SIZE 64u
#define MANGEN_HASH_MAX_HEX_SIZE 129u

typedef enum {
    MANGEN_HASH_SHA256 = 0,
    MANGEN_HASH_SHA384,
    MANGEN_HASH_SHA512
} mangen_hash_algorithm_t;

typedef enum {
    MANGEN_HASH_OK = 0,
    MANGEN_HASH_INVALID_ARGUMENT,
    MANGEN_HASH_BUFFER_TOO_SMALL,
    MANGEN_HASH_OPEN_ERROR,
    MANGEN_HASH_READ_ERROR,
    MANGEN_HASH_CRYPTO_ERROR
} mangen_hash_status_t;

typedef struct {
    uint8_t bytes[MANGEN_HASH_MAX_DIGEST_SIZE];
    size_t size;
} mangen_hash_digest_t;

size_t mangen_hash_digest_size(
    mangen_hash_algorithm_t algorithm
);

mangen_hash_status_t mangen_hash_file(
    const char *path,
    mangen_hash_algorithm_t algorithm,
    mangen_hash_digest_t *out,
    int *error_detail
);

mangen_hash_status_t mangen_hash_to_hex(
    const mangen_hash_digest_t *digest,
    char *out,
    size_t out_capacity
);

#ifdef __cplusplus
}
#endif

#endif
