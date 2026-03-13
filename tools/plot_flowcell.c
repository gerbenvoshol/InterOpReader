/*
 * plot_flowcell — Generate the SAV Analysis Flowcell Heatmap
 *
 * Equivalent to the Illumina SAV "plot_flowcell" application.
 * Reads ExtractionMetrics from a run folder and writes a GNUPlot script
 * showing intensity (channel A, cycle 1) for each tile on the flowcell.
 *
 * Usage:
 *   plot_flowcell <run_folder>
 *   plot_flowcell <run_folder> | gnuplot
 */

#define INTEROP_IMPLEMENTATION
#include "../interop_reader.h"

#include <stdio.h>
#include <stdlib.h>

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s <run_folder>\n\n"
            "Generates the SAV Analysis Flowcell Intensity Heatmap as a GNUPlot script.\n"
            "Pipe the output to gnuplot to display the plot:\n"
            "  %s <run_folder> | gnuplot\n\n"
            "Required file in InterOp/:\n"
            "  ExtractionMetricsOut.bin\n",
            prog, prog);
}

int main(int argc, char *argv[])
{
    if (argc != 2) { usage(argv[0]); return 1; }
    interop_write_flowcell_heatmap_gnuplot(argv[1], stdout);
    return 0;
}
