/*
 * imaging_table — Generate the SAV Imaging Tab table as CSV
 *
 * Equivalent to the Illumina SAV "imaging_table" command-line application.
 * Reads ExtractionMetrics, ErrorMetrics, and CorrectedIntMetrics from a run
 * folder and writes a per-tile, per-cycle imaging table to stdout as CSV.
 *
 * Usage:
 *   imaging_table <run_folder>
 *
 * Output columns:
 *   Lane, Tile, Cycle,
 *   IntensityA, IntensityC, IntensityG, IntensityT,
 *   FWHM_A, FWHM_C, FWHM_G, FWHM_T,
 *   ErrorRate,
 *   NoCall, BaseA, BaseC, BaseG, BaseT
 */

#define INTEROP_IMPLEMENTATION
#include "../interop_reader.h"

#include <stdio.h>
#include <stdlib.h>

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s <run_folder>\n\n"
            "Generates the SAV Imaging Tab table as CSV, written to stdout.\n\n"
            "The run_folder must contain an InterOp/ subdirectory.  The following\n"
            "files are used when present:\n"
            "  ExtractionMetricsOut.bin\n"
            "  ErrorMetricsOut.bin\n"
            "  CorrectedIntMetricsOut.bin\n",
            prog);
}

int main(int argc, char *argv[])
{
    if (argc != 2) { usage(argv[0]); return 1; }
    interop_print_imaging_table_csv(argv[1], stdout);
    return 0;
}
