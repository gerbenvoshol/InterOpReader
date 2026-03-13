/*
 * dumptext — Dump an Illumina InterOp binary file as human-readable text
 *
 * Equivalent to the Illumina SAV "dumptext" command-line application.
 *
 * Usage:
 *   dumptext <InterOp/FileOut.bin>
 *
 * The file type is detected automatically from the filename.
 */

#define INTEROP_IMPLEMENTATION
#include "../interop_reader.h"

#include <stdio.h>
#include <stdlib.h>

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s <InterOp/FileOut.bin>\n\n"
            "Reads a single Illumina InterOp binary file and prints its\n"
            "contents in a human-readable text format to stdout.\n\n"
            "Supported files:\n"
            "  QMetricsOut.bin\n"
            "  TileMetricsOut.bin\n"
            "  CorrectedIntMetricsOut.bin\n"
            "  IndexMetricsOut.bin\n"
            "  ErrorMetricsOut.bin\n"
            "  ExtractionMetricsOut.bin\n"
            "  SummaryRunMetricsOut.bin\n"
            "  ExtendedTileMetricsOut.bin\n",
            prog);
}

int main(int argc, char *argv[])
{
    if (argc != 2) { usage(argv[0]); return 1; }

    const char *filename = argv[1];
    interop_file_type_t type = interop_detect_file_type(filename);

    switch (type) {
    case INTEROP_FILE_QMETRICS: {
        interop_qmetrics_t m = {0};
        if (interop_read_qmetrics(filename, &m) == 0)
            interop_print_qmetrics(&m);
        interop_free_qmetrics(&m);
        break;
    }
    case INTEROP_FILE_TILE_METRICS: {
        interop_tile_metrics_t m = {0};
        if (interop_read_tile_metrics(filename, &m) == 0)
            interop_print_tile_metrics(&m);
        interop_free_tile_metrics(&m);
        break;
    }
    case INTEROP_FILE_CORRECTED_INT: {
        interop_corrected_int_metrics_t m = {0};
        if (interop_read_corrected_int_metrics(filename, &m) == 0)
            interop_print_corrected_int_metrics(&m);
        interop_free_corrected_int_metrics(&m);
        break;
    }
    case INTEROP_FILE_INDEX_METRICS: {
        interop_index_metrics_t m = {0};
        if (interop_read_index_metrics(filename, &m) == 0)
            interop_print_index_metrics(&m);
        interop_free_index_metrics(&m);
        break;
    }
    case INTEROP_FILE_ERROR_METRICS: {
        interop_error_metrics_t m = {0};
        if (interop_read_error_metrics(filename, &m) == 0)
            interop_print_error_metrics(&m);
        interop_free_error_metrics(&m);
        break;
    }
    case INTEROP_FILE_EXTRACTION_METRICS: {
        interop_extraction_metrics_t m = {0};
        if (interop_read_extraction_metrics(filename, &m) == 0)
            interop_print_extraction_metrics(&m);
        interop_free_extraction_metrics(&m);
        break;
    }
    case INTEROP_FILE_SUMMARY_RUN: {
        interop_summary_run_metrics_t m = {0};
        if (interop_read_summary_run(filename, &m) == 0)
            interop_print_summary_run(&m);
        interop_free_summary_run(&m);
        break;
    }
    case INTEROP_FILE_EXTENDED_TILE: {
        interop_extended_tile_metrics_t m = {0};
        if (interop_read_extended_tile_metrics(filename, &m) == 0)
            interop_print_extended_tile_metrics(&m);
        interop_free_extended_tile_metrics(&m);
        break;
    }
    default:
        fprintf(stderr, "Error: unrecognised InterOp file: %s\n\n", filename);
        usage(argv[0]);
        return 1;
    }

    return 0;
}
