/*
 * plot_qscore_heatmap — Generate the SAV Analysis Q-score Heatmap
 *
 * Equivalent to the Illumina SAV "plot_qscore_heatmap" application.
 * Reads QMetrics from a run folder and writes a GNUPlot script to stdout.
 * The heatmap shows Q-score (Y-axis) vs. cycle (X-axis).
 *
 * Usage:
 *   plot_qscore_heatmap <run_folder>
 *   plot_qscore_heatmap <run_folder> | gnuplot
 */

#define INTEROP_IMPLEMENTATION
#include "../interop_reader.h"

#include <stdio.h>
#include <stdlib.h>

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s <run_folder>\n\n"
            "Generates the SAV Analysis Q-score Heatmap as a GNUPlot script.\n"
            "Pipe the output to gnuplot to display the plot:\n"
            "  %s <run_folder> | gnuplot\n\n"
            "Required file in InterOp/:\n"
            "  QMetricsOut.bin\n",
            prog, prog);
}

int main(int argc, char *argv[])
{
    if (argc != 2) { usage(argv[0]); return 1; }
    interop_write_qscore_heatmap_gnuplot(argv[1], stdout);
    return 0;
}
