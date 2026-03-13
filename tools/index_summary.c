/*
 * index_summary — Generate the SAV Indexing Tab summary as CSV
 *
 * Equivalent to the Illumina SAV "index-summary" command-line application.
 * Reads IndexMetrics from a run folder and writes a per-sample cluster count
 * summary to stdout as CSV.
 *
 * Usage:
 *   index_summary <run_folder>
 *
 * Output columns:
 *   Lane, SampleID, IndexName, ProjectName, ClusterCount, FractionMapped
 */

#define INTEROP_IMPLEMENTATION
#include "../interop_reader.h"

#include <stdio.h>
#include <stdlib.h>

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s <run_folder>\n\n"
            "Generates the SAV Indexing Tab summary as CSV, written to stdout.\n\n"
            "The run_folder must contain an InterOp/ subdirectory with:\n"
            "  IndexMetricsOut.bin\n",
            prog);
}

int main(int argc, char *argv[])
{
    if (argc != 2) { usage(argv[0]); return 1; }
    interop_print_index_summary_csv(argv[1], stdout);
    return 0;
}
