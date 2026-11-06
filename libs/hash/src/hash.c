#include "mangen/hash.h"

size_t mangen_hash_digest_size(
    mangen_hash_algorithm_t algorithm
) {
    switch (algorithm) {
        case MANGEN_HASH_SHA256:
            return 32u;

        case MANGEN_HASH_SHA384:
            return 48u;

        case MANGEN_HASH_SHA512:
            return 64u;

        default:
            return 0u;
    }
}

mangen_hash_status_t mangen_hash_to_hex(
    const mangen_hash_digest_t *digest,
    char *out,
    size_t out_capacity
) {
    static const char alphabet[] = "0123456789ABCDEF";

    if (digest == NULL || out == NULL) {
        return MANGEN_HASH_INVALID_ARGUMENT;
    }

    if (digest->size != 32u &&
        digest->size != 48u &&
        digest->size != 64u) {
        return MANGEN_HASH_INVALID_ARGUMENT;
    }

    const size_t required = digest->size * 2u + 1u;

    if (out_capacity < required) {
        return MANGEN_HASH_BUFFER_TOO_SMALL;
    }

    for (size_t i = 0; i < digest->size; ++i) {
        const uint8_t byte = digest->bytes[i];

        out[i * 2u] = alphabet[byte >> 4u];
        out[i * 2u + 1u] = alphabet[byte & 0x0Fu];
    }

    out[digest->size * 2u] = '\0';

    return MANGEN_HASH_OK;
}
