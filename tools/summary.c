/*
 * summary — Generate the SAV Summary Tab table
 *
 * Equivalent to the Illumina SAV "summary" command-line application.
 * Reads InterOp files from a run folder and prints a summary of sequencing
 * run metrics to stdout.
 *
 * Usage:
 *   summary <run_folder>
 *
 * The run folder should contain an InterOp/ subdirectory with the binary
 * metric files.
 */

#define INTEROP_IMPLEMENTATION
#include "../interop_reader.h"

#include <stdio.h>
#include <stdlib.h>

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s <run_folder>\n\n"
            "Generates the SAV Summary Tab metrics for a sequencing run.\n\n"
            "The run_folder must contain an InterOp/ subdirectory with the\n"
            "following files (those present will be used):\n"
            "  QMetricsOut.bin\n"
            "  TileMetricsOut.bin\n"
            "  ErrorMetricsOut.bin\n"
            "  ExtractionMetricsOut.bin\n"
            "  SummaryRunMetricsOut.bin\n",
            prog);
}

int main(int argc, char *argv[])
{
    if (argc != 2) { usage(argv[0]); return 1; }
    interop_print_run_summary(argv[1], stdout);
    return 0;
}
