/*
 * plot_sample_qc — Generate the SAV Indexing Sample QC Plot
 *
 * Equivalent to the Illumina SAV "plot_sample_qc" application.
 * Reads IndexMetrics from a run folder and writes a GNUPlot script showing
 * cluster counts per sample index to stdout.
 *
 * Usage:
 *   plot_sample_qc <run_folder>
 *   plot_sample_qc <run_folder> | gnuplot
 */

#define INTEROP_IMPLEMENTATION
#include "../interop_reader.h"

#include <stdio.h>
#include <stdlib.h>

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s <run_folder>\n\n"
            "Generates the SAV Indexing Sample QC plot as a GNUPlot script.\n"
            "Pipe the output to gnuplot to display the plot:\n"
            "  %s <run_folder> | gnuplot\n\n"
            "Required file in InterOp/:\n"
            "  IndexMetricsOut.bin\n",
            prog, prog);
}

int main(int argc, char *argv[])
{
    if (argc != 2) { usage(argv[0]); return 1; }
    interop_write_sample_qc_gnuplot(argv[1], stdout);
    return 0;
}
