#ifndef MANGEN_CLI_H
#define MANGEN_CLI_H

#include <stddef.h>
#include <stdio.h>

#include "mangen/hash.h"

typedef enum {
    MANGEN_CLI_OK = 0,
    MANGEN_CLI_HELP,
    MANGEN_CLI_INVALID_ARGUMENT,
    MANGEN_CLI_OUT_OF_MEMORY
} mangen_cli_status_t;

typedef struct {
    const char *root_path;

    mangen_hash_algorithm_t algorithm;

    size_t thread_count;
    size_t queue_capacity;

    const char **exclusions;
    size_t exclusion_count;
} mangen_cli_options_t;

/*
 * Parses command-line arguments.
 *
 * Supported options:
 *   -h, --help
 *   --sha256
 *   --sha384
 *   --sha512
 *   -j N, --threads N
 *   --queue-size N
 *   -e PATTERN
 *
 * DIR_PATH is optional and defaults to ".".
 *
 * root_path and exclusion strings reference argv.
 * The exclusions array is allocated by the parser.
 */
mangen_cli_status_t mangen_cli_parse(
    int argc,
    char *const argv[],
    mangen_cli_options_t *options
);

/*
 * Releases allocations owned by options.
 * Does not free root_path or exclusion strings.
 */
void mangen_cli_options_destroy(
    mangen_cli_options_t *options
);

void mangen_cli_print_usage(
    FILE *stream,
    const char *program_name
);

#endif
