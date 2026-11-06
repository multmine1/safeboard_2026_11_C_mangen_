#include "cli.h"
#include "runner.h"

#include <stdio.h>
#include <string.h>

#ifndef MANGEN_SOURCE_COMMIT
#define MANGEN_SOURCE_COMMIT "unknown"
#endif

#ifndef MANGEN_VERSION
#define MANGEN_VERSION "dev"
#endif

int main(int argc, char *argv[]) {
    if (
        argc == 2 &&
        (
            strcmp(argv[1], "-v") == 0 ||
            strcmp(argv[1], "--version") == 0
        )
    ) {
        printf(
            "mangen %s\n"
            "Author: Agibalov Vladislav Sergeevich\n"
            "Git commit: %s\n",
            MANGEN_VERSION,
            MANGEN_SOURCE_COMMIT
        );

        return ferror(stdout) ? 1 : 0;
    }

    mangen_cli_options_t options = {0};

    const mangen_cli_status_t cli_status =
        mangen_cli_parse(
            argc,
            argv,
            &options
        );

    if (cli_status == MANGEN_CLI_HELP) {
        mangen_cli_print_usage(stdout, argv[0]);
        mangen_cli_options_destroy(&options);

        return ferror(stdout) ? 1 : 0;
    }

    if (cli_status == MANGEN_CLI_INVALID_ARGUMENT) {
        fprintf(stderr, "mangen: invalid arguments\n\n");

        mangen_cli_print_usage(stderr, argv[0]);
        mangen_cli_options_destroy(&options);

        return 2;
    }

    if (cli_status == MANGEN_CLI_OUT_OF_MEMORY) {
        fprintf(stderr, "mangen: out of memory\n");

        mangen_cli_options_destroy(&options);

        return 1;
    }

    if (cli_status != MANGEN_CLI_OK) {
        fprintf(stderr, "mangen: CLI initialization failed\n");

        mangen_cli_options_destroy(&options);

        return 1;
    }

    mangen_run_report_t report = {0};

    const mangen_run_status_t run_status =
        mangen_run(
            &options,
            stdout,
            stderr,
            &report
        );

    mangen_cli_options_destroy(&options);

    if (run_status == MANGEN_RUN_OK) {
        return 0;
    }

    if (run_status == MANGEN_RUN_PARTIAL_ERROR) {
        fprintf(
            stderr,
            "mangen: completed with errors "
            "(filesystem=%zu, hashing=%zu)\n",
            report.filesystem_errors,
            report.hashes_failed
        );

        return 1;
    }

    if (run_status == MANGEN_RUN_CANCELLED) {
        fprintf(stderr, "mangen: processing cancelled\n");
        return 1;
    }

    fprintf(stderr, "mangen: processing failed\n");
    return 1;
}
