#include "manifest_internal.h"

#include <stddef.h>
#include <stdint.h>

mangen_manifest_status_t mangen_manifest_hex(
    const uint8_t *digest,
    size_t digest_size,
    char *output,
    size_t output_capacity
) {
    static const char digits[] = "0123456789ABCDEF";

    if (digest == NULL || output == NULL) {
        return MANGEN_MANIFEST_INVALID_ARGUMENT;
    }

    if (digest_size != 32u &&
        digest_size != 48u &&
        digest_size != 64u) {
        return MANGEN_MANIFEST_INVALID_ARGUMENT;
    }

    const size_t required_capacity = digest_size * 2u + 1u;

    if (output_capacity < required_capacity) {
        return MANGEN_MANIFEST_INVALID_ARGUMENT;
    }

    for (size_t i = 0u; i < digest_size; ++i) {
        const uint8_t byte = digest[i];

        output[i * 2u] = digits[byte >> 4u];
        output[i * 2u + 1u] = digits[byte & 0x0Fu];
    }

    output[digest_size * 2u] = '\0';

    return MANGEN_MANIFEST_OK;
}
