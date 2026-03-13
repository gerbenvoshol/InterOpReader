/*
 * plot_by_cycle — Generate the SAV Analysis By-Cycle Plot
 *
 * Equivalent to the Illumina SAV "plot_by_cycle" application.
 * Reads ExtractionMetrics from a run folder and writes a GNUPlot script
 * showing mean channel intensity per cycle to stdout.
 *
 * Usage:
 *   plot_by_cycle <run_folder>
 *   plot_by_cycle <run_folder> | gnuplot
 */

#define INTEROP_IMPLEMENTATION
#include "../interop_reader.h"

#include <stdio.h>
#include <stdlib.h>

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s <run_folder>\n\n"
            "Generates the SAV Analysis Intensity-by-Cycle plot as a GNUPlot script.\n"
            "Pipe the output to gnuplot to display the plot:\n"
            "  %s <run_folder> | gnuplot\n\n"
            "Required file in InterOp/:\n"
            "  ExtractionMetricsOut.bin\n",
            prog, prog);
}

int main(int argc, char *argv[])
{
    if (argc != 2) { usage(argv[0]); return 1; }
    interop_write_plot_by_cycle_gnuplot(argv[1], stdout);
    return 0;
}
