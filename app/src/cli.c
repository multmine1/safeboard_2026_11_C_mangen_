#include "cli.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MANGEN_DEFAULT_THREADS 4u
#define MANGEN_DEFAULT_QUEUE_CAPACITY 16384u

static int parse_positive_size(
    const char *text,
    size_t *result
) {
    if (text == NULL || result == NULL || text[0] == '\0') {
        return 0;
    }

    for (const char *p = text; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
    }

    errno = 0;

    char *end = NULL;
    const uintmax_t value = strtoumax(text, &end, 10);

    if (errno == ERANGE ||
        end == text ||
        *end != '\0' ||
        value == 0u ||
        value > (uintmax_t)SIZE_MAX) {
        return 0;
    }

    *result = (size_t)value;

    return 1;
}

static int append_exclusion(
    mangen_cli_options_t *options,
    const char *pattern,
    int argc
) {
    if (options == NULL ||
        pattern == NULL ||
        pattern[0] == '\0' ||
        argc <= 0) {
        return 0;
    }

    if (options->exclusions == NULL) {
        const size_t capacity = (size_t)argc;

        if (capacity > SIZE_MAX / sizeof(*options->exclusions)) {
            return 0;
        }

        options->exclusions = malloc(
            capacity * sizeof(*options->exclusions)
        );

        if (options->exclusions == NULL) {
            return 0;
        }
    }

    options->exclusions[options->exclusion_count++] = pattern;

    return 1;
}

void mangen_cli_options_destroy(
    mangen_cli_options_t *options
) {
    if (options == NULL) {
        return;
    }

    free(options->exclusions);
    memset(options, 0, sizeof(*options));
}

void mangen_cli_print_usage(
    FILE *stream,
    const char *program_name
) {
    if (stream == NULL) {
        return;
    }

    const char *name = program_name != NULL
        ? program_name
        : "mangen";

    fprintf(
        stream,
        "Usage: %s [DIR_PATH] [OPTIONS]\n"
        "\n"
        "Arguments:\n"
        "  DIR_PATH           Root directory (default: .)\n"
        "\n"
        "Hash algorithms:\n"
        "  --sha256           SHA-256 (default)\n"
        "  --sha384           SHA-384\n"
        "  --sha512           SHA-512\n"
        "\n"
        "Concurrency:\n"
        "  -j, --threads N    Worker threads (default: %u)\n"
        "  --queue-size N     Queue capacity (default: %u)\n"
        "\n"
        "Filtering:\n"
        "  -e PATTERN         Exclude matching basenames\n"
        "                     May be specified multiple times\n"
        "\n"
        "Other:\n"
        "  -h, --help         Show this help\n"
        "\n"
        "Output:\n"
        "  Relative paths and uppercase hexadecimal digests\n"
        "  are written to stdout in lexicographic order.\n",
        name,
        MANGEN_DEFAULT_THREADS,
        MANGEN_DEFAULT_QUEUE_CAPACITY
    );
}

mangen_cli_status_t mangen_cli_parse(
    int argc,
    char *const argv[],
    mangen_cli_options_t *options
) {
    if (argc < 1 || argv == NULL || options == NULL) {
        return MANGEN_CLI_INVALID_ARGUMENT;
    }

    memset(options, 0, sizeof(*options));

    options->root_path = ".";
    options->algorithm = MANGEN_HASH_SHA256;
    options->thread_count = MANGEN_DEFAULT_THREADS;
    options->queue_capacity = MANGEN_DEFAULT_QUEUE_CAPACITY;

    int root_specified = 0;
    int parse_options = 1;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (arg == NULL) {
            goto invalid_argument;
        }

        if (parse_options && strcmp(arg, "--") == 0) {
            parse_options = 0;
            continue;
        }

        if (parse_options &&
            (strcmp(arg, "-h") == 0 ||
             strcmp(arg, "--help") == 0)) {
            return MANGEN_CLI_HELP;
        }

        if (parse_options && strcmp(arg, "--sha256") == 0) {
            options->algorithm = MANGEN_HASH_SHA256;
            continue;
        }

        if (parse_options && strcmp(arg, "--sha384") == 0) {
            options->algorithm = MANGEN_HASH_SHA384;
            continue;
        }

        if (parse_options && strcmp(arg, "--sha512") == 0) {
            options->algorithm = MANGEN_HASH_SHA512;
            continue;
        }

        if (parse_options &&
            (strcmp(arg, "-j") == 0 ||
             strcmp(arg, "--threads") == 0)) {
            if (++i >= argc ||
                !parse_positive_size(
                    argv[i],
                    &options->thread_count
                )) {
                goto invalid_argument;
            }

            continue;
        }

        if (parse_options &&
            strncmp(arg, "--threads=", 10u) == 0) {
            if (!parse_positive_size(
                    arg + 10u,
                    &options->thread_count
                )) {
                goto invalid_argument;
            }

            continue;
        }

        if (parse_options &&
            strncmp(arg, "-j", 2u) == 0 &&
            arg[2] != '\0') {
            if (!parse_positive_size(
                    arg + 2u,
                    &options->thread_count
                )) {
                goto invalid_argument;
            }

            continue;
        }

        if (parse_options &&
            strcmp(arg, "--queue-size") == 0) {
            if (++i >= argc ||
                !parse_positive_size(
                    argv[i],
                    &options->queue_capacity
                )) {
                goto invalid_argument;
            }

            continue;
        }

        if (parse_options &&
            strncmp(arg, "--queue-size=", 13u) == 0) {
            if (!parse_positive_size(
                    arg + 13u,
                    &options->queue_capacity
                )) {
                goto invalid_argument;
            }

            continue;
        }

        if (parse_options && strcmp(arg, "-e") == 0) {
            if (++i >= argc || argv[i] == NULL) {
                goto invalid_argument;
            }

            if (!append_exclusion(
                    options,
                    argv[i],
                    argc
                )) {
                goto allocation_or_argument_error;
            }

            continue;
        }

        if (parse_options &&
            strncmp(arg, "-e", 2u) == 0 &&
            arg[2] != '\0') {
            if (!append_exclusion(
                    options,
                    arg + 2u,
                    argc
                )) {
                goto allocation_or_argument_error;
            }

            continue;
        }

        if (parse_options && arg[0] == '-') {
            goto invalid_argument;
        }

        if (arg[0] == '\0' || root_specified) {
            goto invalid_argument;
        }

        options->root_path = arg;
        root_specified = 1;
    }

    return MANGEN_CLI_OK;

allocation_or_argument_error:
    {
        const mangen_cli_status_t status =
            options->exclusions == NULL
                ? MANGEN_CLI_OUT_OF_MEMORY
                : MANGEN_CLI_INVALID_ARGUMENT;

        mangen_cli_options_destroy(options);
        return status;
    }

invalid_argument:
    mangen_cli_options_destroy(options);
    return MANGEN_CLI_INVALID_ARGUMENT;
}
