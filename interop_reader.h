/**
 * @file interop_reader.h
 * @brief Single-header C library for reading Illumina InterOp binary files (STB-style)
 *
 * USAGE:
 *   In exactly ONE .c/.cpp file, before including this header:
 *     #define INTEROP_IMPLEMENTATION
 *   Then include as normal in that file and all others:
 *     #include "interop_reader.h"
 *
 * SUPPORTED FILE FORMATS:
 *   - QMetricsOut.bin             Q-score metrics (versions 4-7)
 *   - TileMetricsOut.bin          Tile metrics (version 2)
 *   - CorrectedIntMetricsOut.bin  Corrected intensity metrics (versions 2-4)
 *   - IndexMetricsOut.bin         Index (barcode) metrics (versions 1-2)
 *   - ErrorMetricsOut.bin         PhiX error metrics (versions 3-6)
 *   - ExtractionMetricsOut.bin    Extraction/intensity metrics (versions 2-3)
 *   - SummaryRunMetricsOut.bin    Run-level summary metrics (version 1)
 *   - ExtendedTileMetricsOut.bin  Extended tile occupancy metrics (versions 1-3)
 *
 * HIGH-LEVEL TOOLS (operate on a run folder, equivalent to Illumina SAV tabs):
 *   interop_print_run_summary()          SAV Summary Tab
 *   interop_print_imaging_table_csv()    SAV Imaging Tab (CSV)
 *   interop_print_index_summary_csv()    SAV Indexing Tab (CSV)
 *   interop_write_qscore_histogram_gnuplot()  SAV Analysis: Q-score Histogram
 *   interop_write_qscore_heatmap_gnuplot()    SAV Analysis: Q-score Heatmap
 *   interop_write_plot_by_cycle_gnuplot()     SAV Analysis: Plot by Cycle
 *   interop_write_plot_by_lane_gnuplot()      SAV Analysis: Plot by Lane
 *   interop_write_flowcell_heatmap_gnuplot()  SAV Analysis: Flowcell Heatmap
 *   interop_write_sample_qc_gnuplot()         SAV Indexing: Sample QC Plot
 *
 * LICENSE: This is free and unencumbered software released into the public domain.
 *          See LICENSE for full terms.
 */

#ifndef INTEROP_READER_H
#define INTEROP_READER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* ============================================================
 * CONSTANTS
 * ============================================================ */

/** Maximum number of Q-score bins (covers Q1..Q50). */
#define INTEROP_MAX_Q_BINS    50

/** Maximum length (bytes) for index/sample/project name strings. */
#define INTEROP_MAX_NAME_LEN  512

/** Number of base channels: A, C, G, T. */
#define INTEROP_NUM_CHANNELS  4

/** Maximum number of PhiX error classes tracked per cycle. */
#define INTEROP_NUM_ERROR_CLASSES 5

/* Tile metric code values (stored in the code field of tile records) */
#define INTEROP_TILE_DENSITY            100 /**< Cluster density (k/mm²) */
#define INTEROP_TILE_DENSITY_PF         101 /**< Cluster density PF (k/mm²) */
#define INTEROP_TILE_CLUSTER_COUNT      102 /**< Cluster count */
#define INTEROP_TILE_CLUSTER_COUNT_PF   103 /**< Cluster count PF */
/* For reads: aligned = 200+2*(read-1), phasing = 201+2*(read-1), prephasing = 202+2*(read-1) */

/* ============================================================
 * FILE TYPE ENUMERATION
 * ============================================================ */

/** Identifies which InterOp file type a path refers to. */
typedef enum interop_file_type {
    INTEROP_FILE_UNKNOWN            = 0,
    INTEROP_FILE_QMETRICS           = 1,
    INTEROP_FILE_TILE_METRICS       = 2,
    INTEROP_FILE_CORRECTED_INT      = 3,
    INTEROP_FILE_INDEX_METRICS      = 4,
    INTEROP_FILE_ERROR_METRICS      = 5,
    INTEROP_FILE_EXTRACTION_METRICS = 6,
    INTEROP_FILE_SUMMARY_RUN        = 7,
    INTEROP_FILE_EXTENDED_TILE      = 8
} interop_file_type_t;

/* ============================================================
 * DATA STRUCTURES
 * ============================================================ */

/**
 * Q-score metric record: per-lane/tile/cycle Q-score histogram.
 * histogram[i] contains the read count for bin i.
 * num_bins is set to the actual number of active bins (<= INTEROP_MAX_Q_BINS).
 */
typedef struct {
    uint16_t lane;
    uint32_t tile;
    uint16_t cycle;
    uint32_t histogram[INTEROP_MAX_Q_BINS];
    uint8_t  num_bins;
} interop_qmetric_record_t;

/** Collection of Q-score metric records plus header metadata. */
typedef struct {
    uint8_t  version;
    uint8_t  num_bins;
    uint8_t  bin_low  [INTEROP_MAX_Q_BINS]; /**< Lower bound of each Q-score bin */
    uint8_t  bin_high [INTEROP_MAX_Q_BINS]; /**< Upper bound of each Q-score bin */
    uint8_t  bin_value[INTEROP_MAX_Q_BINS]; /**< Representative Q-score for each bin */
    interop_qmetric_record_t *records;
    size_t   count;
    size_t   capacity;
} interop_qmetrics_t;

/**
 * Tile metric record: a single metric value for one lane/tile.
 * The meaning of value depends on code (see INTEROP_TILE_* constants).
 */
typedef struct {
    uint16_t lane;
    uint32_t tile;
    uint16_t code;  /**< Metric identifier (e.g. INTEROP_TILE_CLUSTER_COUNT) */
    float    value;
} interop_tile_record_t;

/** Collection of tile metric records. */
typedef struct {
    uint8_t  version;
    float    density;    /**< v3+: cluster density from file header (k/mm²) */
    interop_tile_record_t *records;
    size_t   count;
    size_t   capacity;
} interop_tile_metrics_t;

/**
 * Corrected intensity metric record: per-lane/tile/cycle base-call counts.
 * base_count[0..3] = counts for A, C, G, T respectively.
 */
typedef struct {
    uint16_t lane;
    uint32_t tile;
    uint16_t cycle;
    uint32_t no_call;
    uint32_t base_count[INTEROP_NUM_CHANNELS]; /**< A, C, G, T */
} interop_corrected_int_record_t;

/** Collection of corrected intensity metric records. */
typedef struct {
    uint8_t  version;
    interop_corrected_int_record_t *records;
    size_t   count;
    size_t   capacity;
} interop_corrected_int_metrics_t;

/**
 * Index metric record: per-lane/tile/read index assignment.
 * Records the number of clusters assigned to each barcode index.
 */
typedef struct {
    uint16_t lane;
    uint32_t tile;
    uint16_t read;
    char     index_name  [INTEROP_MAX_NAME_LEN];
    uint64_t cluster_count;
    char     sample_name [INTEROP_MAX_NAME_LEN];
    char     project_name[INTEROP_MAX_NAME_LEN];
} interop_index_record_t;

/** Collection of index metric records. */
typedef struct {
    uint8_t  version;
    interop_index_record_t *records;
    size_t   count;
    size_t   capacity;
} interop_index_metrics_t;

/**
 * Error metric record: per-lane/tile/cycle PhiX error rate.
 * phix_read_count[0..4] = reads with 0,1,2,3,4 errors respectively.
 */
typedef struct {
    uint16_t lane;
    uint32_t tile;
    uint16_t cycle;
    float    error_rate;
    uint32_t phix_read_count[INTEROP_NUM_ERROR_CLASSES];
} interop_error_record_t;

/** Collection of error metric records. */
typedef struct {
    uint8_t  version;
    interop_error_record_t *records;
    size_t   count;
    size_t   capacity;
} interop_error_metrics_t;

/**
 * Extraction metric record: per-lane/tile/cycle channel intensities and FWHM.
 * intensity[0..3] and fwhm[0..3] correspond to channels A, C, G, T.
 */
typedef struct {
    uint16_t lane;
    uint32_t tile;
    uint16_t cycle;
    int16_t  intensity[INTEROP_NUM_CHANNELS]; /**< Mean intensity A, C, G, T */
    float    fwhm     [INTEROP_NUM_CHANNELS]; /**< Full-width half-max A, C, G, T */
} interop_extraction_record_t;

/** Collection of extraction metric records. */
typedef struct {
    uint8_t  version;
    interop_extraction_record_t *records;
    size_t   count;
    size_t   capacity;
} interop_extraction_metrics_t;

/** Run-level summary metric record (SummaryRunMetricsOut.bin). */
typedef struct {
    double occupancy_proxy_cluster_count;
    double raw_cluster_count;
    double occupancy_cluster_count;
    double pf_cluster_count;
} interop_summary_run_record_t;

/** Collection of run summary metric records. */
typedef struct {
    uint8_t  version;
    interop_summary_run_record_t *records;
    size_t   count;
    size_t   capacity;
} interop_summary_run_metrics_t;

/** Extended tile metric record: occupied cluster count per tile. */
typedef struct {
    uint16_t lane;
    uint32_t tile;
    float    cluster_count_occupied;
} interop_extended_tile_record_t;

/** Collection of extended tile metric records. */
typedef struct {
    uint8_t  version;
    interop_extended_tile_record_t *records;
    size_t   count;
    size_t   capacity;
} interop_extended_tile_metrics_t;

/* ============================================================
 * PUBLIC API — FILE UTILITIES
 * ============================================================ */

/**
 * Detect the InterOp file type from a file path.
 * Returns INTEROP_FILE_UNKNOWN if the filename is not recognised.
 */
interop_file_type_t interop_detect_file_type(const char *filename);

/**
 * Build the path to an InterOp file within a run folder.
 *
 * Tries both "<run_folder>/InterOp/<metric>Out.bin" and
 * "<run_folder>/<metric>Out.bin".
 *
 * @param run_folder  Path to the sequencing run folder.
 * @param metric_name Name of the metric (e.g. "QMetrics").
 * @param out_path    Output buffer for the full path.
 * @param out_size    Size of the output buffer in bytes.
 * @return 1 if a readable file was found, 0 otherwise.
 */
int interop_build_filepath(const char *run_folder, const char *metric_name,
                           char *out_path, size_t out_size);

/* ============================================================
 * PUBLIC API — PARSERS
 * ============================================================ */

/**
 * Read a QMetricsOut.bin file.
 * @param filename  Path to the file.
 * @param out       Caller-owned structure populated on success.
 * @return 0 on success, non-zero on error.
 * @note Call interop_free_qmetrics() when done.
 */
int interop_read_qmetrics(const char *filename, interop_qmetrics_t *out);

/** Read a TileMetricsOut.bin file. */
int interop_read_tile_metrics(const char *filename, interop_tile_metrics_t *out);

/** Read a CorrectedIntMetricsOut.bin file. */
int interop_read_corrected_int_metrics(const char *filename,
                                       interop_corrected_int_metrics_t *out);

/** Read an IndexMetricsOut.bin file. */
int interop_read_index_metrics(const char *filename, interop_index_metrics_t *out);

/** Read an ErrorMetricsOut.bin file. */
int interop_read_error_metrics(const char *filename, interop_error_metrics_t *out);

/** Read an ExtractionMetricsOut.bin file. */
int interop_read_extraction_metrics(const char *filename,
                                    interop_extraction_metrics_t *out);

/** Read a SummaryRunMetricsOut.bin file. */
int interop_read_summary_run(const char *filename, interop_summary_run_metrics_t *out);

/** Read an ExtendedTileMetricsOut.bin file. */
int interop_read_extended_tile_metrics(const char *filename,
                                       interop_extended_tile_metrics_t *out);

/* ============================================================
 * PUBLIC API — MEMORY MANAGEMENT
 * ============================================================ */

void interop_free_qmetrics           (interop_qmetrics_t *m);
void interop_free_tile_metrics       (interop_tile_metrics_t *m);
void interop_free_corrected_int_metrics(interop_corrected_int_metrics_t *m);
void interop_free_index_metrics      (interop_index_metrics_t *m);
void interop_free_error_metrics      (interop_error_metrics_t *m);
void interop_free_extraction_metrics (interop_extraction_metrics_t *m);
void interop_free_summary_run        (interop_summary_run_metrics_t *m);
void interop_free_extended_tile_metrics(interop_extended_tile_metrics_t *m);

/* ============================================================
 * PUBLIC API — AGGREGATE STATISTICS
 * ============================================================ */

/** Compute the total read count summed across all records. */
uint64_t interop_compute_total_reads(const interop_qmetrics_t *m);

/** Compute the total Q>=30 read count summed across all records. */
uint64_t interop_compute_q30_reads(const interop_qmetrics_t *m);

/** Compute the overall percent of reads with Q>=30. */
double interop_compute_percent_q30(const interop_qmetrics_t *m);

/**
 * Compute the average error rate.
 * @param lane  Lane number, or -1 to average across all lanes.
 */
double interop_compute_avg_error_rate(const interop_error_metrics_t *m, int lane);

/**
 * Compute the average intensity at cycle 1.
 * @param channel  0=A, 1=C, 2=G, 3=T.
 * @param lane     Lane number, or -1 for all lanes.
 */
double interop_compute_intensity_cycle1(const interop_extraction_metrics_t *m,
                                        int channel, int lane);

/* ============================================================
 * PUBLIC API — HUMAN-READABLE PRINT FUNCTIONS
 * ============================================================ */

void interop_print_qmetrics              (const interop_qmetrics_t *m);
void interop_print_tile_metrics          (const interop_tile_metrics_t *m);
void interop_print_corrected_int_metrics (const interop_corrected_int_metrics_t *m);
void interop_print_index_metrics         (const interop_index_metrics_t *m);
void interop_print_error_metrics         (const interop_error_metrics_t *m);
void interop_print_extraction_metrics    (const interop_extraction_metrics_t *m);
void interop_print_summary_run           (const interop_summary_run_metrics_t *m);
void interop_print_extended_tile_metrics (const interop_extended_tile_metrics_t *m);

/* ============================================================
 * PUBLIC API — CSV DUMP FUNCTIONS
 * ============================================================ */

void interop_dump_qmetrics_csv              (const interop_qmetrics_t *m, FILE *out);
void interop_dump_tile_metrics_csv          (const interop_tile_metrics_t *m, FILE *out);
void interop_dump_corrected_int_metrics_csv (const interop_corrected_int_metrics_t *m, FILE *out);
void interop_dump_index_metrics_csv         (const interop_index_metrics_t *m, FILE *out);
void interop_dump_error_metrics_csv         (const interop_error_metrics_t *m, FILE *out);
void interop_dump_extraction_metrics_csv    (const interop_extraction_metrics_t *m, FILE *out);
void interop_dump_summary_run_csv           (const interop_summary_run_metrics_t *m, FILE *out);
void interop_dump_extended_tile_metrics_csv (const interop_extended_tile_metrics_t *m, FILE *out);

/* ============================================================
 * PUBLIC API — HIGH-LEVEL RUN-FOLDER TOOLS
 * (Equivalent to the Illumina SAV command-line applications)
 * ============================================================ */

/**
 * Print a run summary table (SAV Summary Tab equivalent).
 * Reads QMetrics, TileMetrics, ErrorMetrics, ExtractionMetrics, and
 * SummaryRunMetrics from the run folder.
 * @param run_folder  Path to the sequencing run folder.
 * @param out         Output stream.
 */
void interop_print_run_summary(const char *run_folder, FILE *out);

/**
 * Print the SAV Imaging Tab table as CSV.
 * Reads ExtractionMetrics, ErrorMetrics, and CorrectedIntMetrics.
 * @param run_folder  Path to the sequencing run folder.
 * @param out         Output stream.
 */
void interop_print_imaging_table_csv(const char *run_folder, FILE *out);

/**
 * Print the SAV Indexing Tab summary as CSV.
 * Reads IndexMetrics, aggregating cluster counts per sample/index.
 * @param run_folder  Path to the sequencing run folder.
 * @param out         Output stream.
 */
void interop_print_index_summary_csv(const char *run_folder, FILE *out);

/**
 * Write a Q-score histogram as a GNUPlot script (SAV Analysis Tab).
 * @param run_folder  Path to the sequencing run folder.
 * @param out         Output stream.
 */
void interop_write_qscore_histogram_gnuplot(const char *run_folder, FILE *out);

/**
 * Write a Q-score vs. cycle heatmap as a GNUPlot script (SAV Analysis Tab).
 * @param run_folder  Path to the sequencing run folder.
 * @param out         Output stream.
 */
void interop_write_qscore_heatmap_gnuplot(const char *run_folder, FILE *out);

/**
 * Write an intensity-by-cycle plot as a GNUPlot script (SAV Analysis Tab).
 * @param run_folder  Path to the sequencing run folder.
 * @param out         Output stream.
 */
void interop_write_plot_by_cycle_gnuplot(const char *run_folder, FILE *out);

/**
 * Write a cluster-count-by-lane plot as a GNUPlot script (SAV Analysis Tab).
 * @param run_folder  Path to the sequencing run folder.
 * @param out         Output stream.
 */
void interop_write_plot_by_lane_gnuplot(const char *run_folder, FILE *out);

/**
 * Write a flowcell intensity heatmap as a GNUPlot script (SAV Analysis Tab).
 * @param run_folder  Path to the sequencing run folder.
 * @param out         Output stream.
 */
void interop_write_flowcell_heatmap_gnuplot(const char *run_folder, FILE *out);

/**
 * Write a sample index QC bar chart as a GNUPlot script (SAV Indexing Tab).
 * @param run_folder  Path to the sequencing run folder.
 * @param out         Output stream.
 */
void interop_write_sample_qc_gnuplot(const char *run_folder, FILE *out);

/* ============================================================
 * IMPLEMENTATION
 * ============================================================ */

#ifdef INTEROP_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>

/* ----------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------- */

#define INTEROP__INIT_CAP 64

/**
 * Grow a dynamic array so it can hold at least count+1 elements.
 * Returns 1 on success, 0 on out-of-memory.
 */
static int interop__ensure_cap(void **arr, size_t count, size_t *cap, size_t elem_size)
{
    if (count < *cap) return 1;
    size_t new_cap = *cap ? *cap * 2 : INTEROP__INIT_CAP;
    void *tmp = realloc(*arr, new_cap * elem_size);
    if (!tmp) return 0;
    *arr = tmp;
    *cap = new_cap;
    return 1;
}

/** Read exactly one item of the given size; return 1 on success, 0 on failure. */
static int interop__fread1(void *buf, size_t size, FILE *f)
{
    return fread(buf, size, 1, f) == 1;
}

/**
 * Read a variable-length string of exactly 'len' bytes from file into buf.
 * buf is always NUL-terminated. Excess bytes beyond buf_size-1 are skipped.
 * Returns 1 on success (or if len==0), 0 on I/O error.
 */
static int interop__fread_str(char *buf, uint16_t len, size_t buf_size, FILE *f)
{
    if (len == 0) { buf[0] = '\0'; return 1; }
    uint16_t to_read = (len < (uint16_t)(buf_size - 1)) ? len : (uint16_t)(buf_size - 1);
    size_t n = fread(buf, 1, to_read, f);
    buf[n] = '\0';
    if (len > to_read) {
        /* skip the excess bytes */
        fseek(f, (long)(len - to_read), SEEK_CUR);
    }
    return (n == to_read);
}

/* ----------------------------------------------------------
 * interop_detect_file_type
 * ---------------------------------------------------------- */

interop_file_type_t interop_detect_file_type(const char *filename)
{
    if (!filename) return INTEROP_FILE_UNKNOWN;
    /* Use only the basename so path components do not mislead */
    const char *base = strrchr(filename, '/');
    if (!base) base = strrchr(filename, '\\');
    base = base ? base + 1 : filename;

    if (strstr(base, "QMetrics"))            return INTEROP_FILE_QMETRICS;
    if (strstr(base, "ExtendedTile"))        return INTEROP_FILE_EXTENDED_TILE;
    if (strstr(base, "TileMetrics"))         return INTEROP_FILE_TILE_METRICS;
    if (strstr(base, "CorrectedInt"))        return INTEROP_FILE_CORRECTED_INT;
    if (strstr(base, "IndexMetrics"))        return INTEROP_FILE_INDEX_METRICS;
    if (strstr(base, "ErrorMetrics"))        return INTEROP_FILE_ERROR_METRICS;
    if (strstr(base, "ExtractionMetrics"))   return INTEROP_FILE_EXTRACTION_METRICS;
    if (strstr(base, "SummaryRun"))          return INTEROP_FILE_SUMMARY_RUN;
    return INTEROP_FILE_UNKNOWN;
}

/* ----------------------------------------------------------
 * interop_build_filepath
 * ---------------------------------------------------------- */

int interop_build_filepath(const char *run_folder, const char *metric_name,
                           char *out_path, size_t out_size)
{
    FILE *f;
    /* <run_folder>/InterOp/<metric>Out.bin */
    snprintf(out_path, out_size, "%s/InterOp/%sOut.bin", run_folder, metric_name);
    f = fopen(out_path, "rb");
    if (f) { fclose(f); return 1; }
    /* <run_folder>/<metric>Out.bin (direct InterOp directory) */
    snprintf(out_path, out_size, "%s/%sOut.bin", run_folder, metric_name);
    f = fopen(out_path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

/* ----------------------------------------------------------
 * Q-Metrics parser
 * ---------------------------------------------------------- */

int interop_read_qmetrics(const char *filename, interop_qmetrics_t *out)
{
    FILE *f;
    uint8_t version, record_size, has_bins;
    int i;

    memset(out, 0, sizeof(*out));
    f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "interop: cannot open '%s'\n", filename);
        return 1;
    }
    if (!interop__fread1(&version,     1, f) ||
        !interop__fread1(&record_size, 1, f) ||
        !interop__fread1(&has_bins,    1, f)) {
        fclose(f); return 2;
    }
    out->version = version;

    if (has_bins) {
        uint8_t bin_count;
        if (!interop__fread1(&bin_count, 1, f)) { fclose(f); return 3; }
        if (bin_count > INTEROP_MAX_Q_BINS) bin_count = INTEROP_MAX_Q_BINS;
        out->num_bins = bin_count;
        for (i = 0; i < bin_count; i++) {
            if (!interop__fread1(&out->bin_low[i],   1, f) ||
                !interop__fread1(&out->bin_high[i],  1, f) ||
                !interop__fread1(&out->bin_value[i], 1, f)) {
                fclose(f); return 4;
            }
        }
    } else {
        /* No bin encoding: use Q1..Q50 */
        out->num_bins = INTEROP_MAX_Q_BINS;
        for (i = 0; i < INTEROP_MAX_Q_BINS; i++) {
            out->bin_low[i]   = (uint8_t)(i + 1);
            out->bin_high[i]  = (uint8_t)(i + 1);
            out->bin_value[i] = (uint8_t)(i + 1);
        }
    }

    while (!feof(f)) {
        interop_qmetric_record_t rec;
        memset(&rec, 0, sizeof(rec));
        if (!interop__fread1(&rec.lane,  sizeof(uint16_t), f)) break;
        if (!interop__fread1(&rec.tile,  sizeof(uint32_t), f)) break;
        if (!interop__fread1(&rec.cycle, sizeof(uint16_t), f)) break;
        rec.num_bins = out->num_bins;
        if (fread(rec.histogram, sizeof(uint32_t), out->num_bins, f) !=
                (size_t)out->num_bins) break;
        if (!interop__ensure_cap((void **)&out->records, out->count,
                                 &out->capacity,
                                 sizeof(interop_qmetric_record_t))) break;
        out->records[out->count++] = rec;
    }
    fclose(f);
    return 0;
}

void interop_free_qmetrics(interop_qmetrics_t *m)
{
    if (m) { free(m->records); memset(m, 0, sizeof(*m)); }
}

/* ----------------------------------------------------------
 * Tile Metrics parser
 * ---------------------------------------------------------- */

int interop_read_tile_metrics(const char *filename, interop_tile_metrics_t *out)
{
    FILE *f;
    uint8_t version, record_size;

    memset(out, 0, sizeof(*out));
    f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "interop: cannot open '%s'\n", filename);
        return 1;
    }
    if (!interop__fread1(&version,     1, f) ||
        !interop__fread1(&record_size, 1, f)) {
        fclose(f); return 2;
    }
    out->version = version;

    /* v3+: header contains an additional 4-byte density float before records */
    if (version >= 3) {
        if (!interop__fread1(&out->density, sizeof(float), f)) {
            fclose(f); return 2;
        }
    }

    while (!feof(f)) {
        uint16_t lane;
        uint32_t tile;

        if (!interop__fread1(&lane, sizeof(uint16_t), f)) break;
        if (!interop__fread1(&tile, sizeof(uint32_t), f)) break;

        if (version >= 3) {
            /* v3 record: lane(2) + tile(4) + code(1) + data(8)
             * code 't': cluster_count(4) + pf_cluster_count(4)
             * code 'r': read_number(4)   + pct_aligned(4)       */
            uint8_t code;
            if (!interop__fread1(&code, sizeof(uint8_t), f)) break;

            if (code == 't') {
                float cluster_count, pf_cluster_count;
                interop_tile_record_t rec;
                if (!interop__fread1(&cluster_count,    sizeof(float), f)) break;
                if (!interop__fread1(&pf_cluster_count, sizeof(float), f)) break;
                rec.lane = lane; rec.tile = tile;
                rec.code = INTEROP_TILE_CLUSTER_COUNT; rec.value = cluster_count;
                if (!interop__ensure_cap((void **)&out->records, out->count,
                                         &out->capacity,
                                         sizeof(interop_tile_record_t))) break;
                out->records[out->count++] = rec;
                rec.code = INTEROP_TILE_CLUSTER_COUNT_PF; rec.value = pf_cluster_count;
                if (!interop__ensure_cap((void **)&out->records, out->count,
                                         &out->capacity,
                                         sizeof(interop_tile_record_t))) break;
                out->records[out->count++] = rec;
            } else if (code == 'r') {
                uint32_t read_number;
                float pct_aligned;
                interop_tile_record_t rec;
                if (!interop__fread1(&read_number,  sizeof(uint32_t), f)) break;
                if (!interop__fread1(&pct_aligned,  sizeof(float),    f)) break;
                rec.lane  = lane; rec.tile = tile;
                /* Aligned-pct codes: 200 for read 1, 202 for read 2, etc.
                 * (same scheme as the v2 format: 200 + 2*(read_number-1)) */
                rec.code  = (uint16_t)(200 + 2 * (read_number - 1));
                rec.value = pct_aligned;
                if (!interop__ensure_cap((void **)&out->records, out->count,
                                         &out->capacity,
                                         sizeof(interop_tile_record_t))) break;
                out->records[out->count++] = rec;
            } else {
                /* unknown code: skip the remaining data bytes in this record */
                long skip = (long)record_size -
                            (long)(sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint8_t));
                if (skip > 0) fseek(f, skip, SEEK_CUR);
            }
        } else {
            /* v2 record: lane(2) + tile(4) + code(2) + value(4) = 12 bytes */
            interop_tile_record_t rec;
            rec.lane = lane; rec.tile = tile;
            if (!interop__fread1(&rec.code,  sizeof(uint16_t), f)) break;
            if (!interop__fread1(&rec.value, sizeof(float),    f)) break;
            /* skip any extra bytes present in newer sub-versions */
            if (record_size > (uint8_t)(sizeof(uint16_t) + sizeof(uint32_t) +
                                        sizeof(uint16_t) + sizeof(float)))
                fseek(f, (long)record_size -
                          (long)(sizeof(uint16_t) + sizeof(uint32_t) +
                                 sizeof(uint16_t) + sizeof(float)), SEEK_CUR);
            if (!interop__ensure_cap((void **)&out->records, out->count,
                                     &out->capacity,
                                     sizeof(interop_tile_record_t))) break;
            out->records[out->count++] = rec;
        }
    }
    fclose(f);
    return 0;
}

void interop_free_tile_metrics(interop_tile_metrics_t *m)
{
    if (m) { free(m->records); memset(m, 0, sizeof(*m)); }
}

/* ----------------------------------------------------------
 * Corrected Intensity Metrics parser
 * ---------------------------------------------------------- */

int interop_read_corrected_int_metrics(const char *filename,
                                       interop_corrected_int_metrics_t *out)
{
    FILE *f;
    uint8_t version, record_size;

    memset(out, 0, sizeof(*out));
    f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "interop: cannot open '%s'\n", filename);
        return 1;
    }
    if (!interop__fread1(&version,     1, f) ||
        !interop__fread1(&record_size, 1, f)) {
        fclose(f); return 2;
    }
    out->version = version;

    while (!feof(f)) {
        interop_corrected_int_record_t rec;
        memset(&rec, 0, sizeof(rec));
        if (!interop__fread1(&rec.lane,  sizeof(uint16_t), f)) break;
        if (!interop__fread1(&rec.tile,  sizeof(uint32_t), f)) break;
        if (!interop__fread1(&rec.cycle, sizeof(uint16_t), f)) break;

        if (version == 2) {
            /* v2: avg_corr_int[4](u16) + avg_uncorr_int[4](u16) + no_call(u32) +
             *     base_count[4](u32) + signal_to_noise(f32) */
            uint16_t tmp16[4];
            float    snr;
            if (fread(tmp16, sizeof(uint16_t), 4, f) != 4) break;
            if (fread(tmp16, sizeof(uint16_t), 4, f) != 4) break;
            if (!interop__fread1(&rec.no_call, sizeof(uint32_t), f)) break;
            if (fread(rec.base_count, sizeof(uint32_t), 4, f) != 4) break;
            if (!interop__fread1(&snr, sizeof(float), f)) break;
        } else {
            /* v3, v4: no_call(u32) + base_count[4](u32) */
            if (!interop__fread1(&rec.no_call, sizeof(uint32_t), f)) break;
            if (fread(rec.base_count, sizeof(uint32_t), 4, f) != 4) break;
        }
        if (!interop__ensure_cap((void **)&out->records, out->count,
                                 &out->capacity,
                                 sizeof(interop_corrected_int_record_t))) break;
        out->records[out->count++] = rec;
    }
    fclose(f);
    return 0;
}

void interop_free_corrected_int_metrics(interop_corrected_int_metrics_t *m)
{
    if (m) { free(m->records); memset(m, 0, sizeof(*m)); }
}

/* ----------------------------------------------------------
 * Index Metrics parser
 * ---------------------------------------------------------- */

int interop_read_index_metrics(const char *filename, interop_index_metrics_t *out)
{
    FILE *f;
    uint8_t version;

    memset(out, 0, sizeof(*out));
    f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "interop: cannot open '%s'\n", filename);
        return 1;
    }
    if (!interop__fread1(&version, 1, f)) { fclose(f); return 2; }
    out->version = version;

    while (!feof(f)) {
        interop_index_record_t rec;
        uint16_t name_len;
        memset(&rec, 0, sizeof(rec));
        if (!interop__fread1(&rec.lane, sizeof(uint16_t), f)) break;
        if (!interop__fread1(&rec.tile, sizeof(uint32_t), f)) break;
        if (!interop__fread1(&rec.read, sizeof(uint16_t), f)) break;
        /* index name */
        if (!interop__fread1(&name_len, sizeof(uint16_t), f)) break;
        if (!interop__fread_str(rec.index_name, name_len,
                                sizeof(rec.index_name), f)) break;
        if (!interop__fread1(&rec.cluster_count, sizeof(uint64_t), f)) break;
        /* sample name */
        if (!interop__fread1(&name_len, sizeof(uint16_t), f)) break;
        if (!interop__fread_str(rec.sample_name, name_len,
                                sizeof(rec.sample_name), f)) break;
        /* project name */
        if (!interop__fread1(&name_len, sizeof(uint16_t), f)) break;
        if (!interop__fread_str(rec.project_name, name_len,
                                sizeof(rec.project_name), f)) break;
        if (!interop__ensure_cap((void **)&out->records, out->count,
                                 &out->capacity,
                                 sizeof(interop_index_record_t))) break;
        out->records[out->count++] = rec;
    }
    fclose(f);
    return 0;
}

void interop_free_index_metrics(interop_index_metrics_t *m)
{
    if (m) { free(m->records); memset(m, 0, sizeof(*m)); }
}

/* ----------------------------------------------------------
 * Error Metrics parser
 * ---------------------------------------------------------- */

int interop_read_error_metrics(const char *filename, interop_error_metrics_t *out)
{
    FILE *f;
    uint8_t version, record_size;

    memset(out, 0, sizeof(*out));
    f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "interop: cannot open '%s'\n", filename);
        return 1;
    }
    if (!interop__fread1(&version,     1, f) ||
        !interop__fread1(&record_size, 1, f)) {
        fclose(f); return 2;
    }
    out->version = version;

    /* v6+: header has extra fields before records:
     *   num_reads(2) + per_read_bytes(2) + data[num_reads * per_read_bytes]
     * v3..v5: no extra header fields */
    if (version >= 6) {
        uint16_t num_reads, per_read_bytes;
        if (!interop__fread1(&num_reads,      sizeof(uint16_t), f) ||
            !interop__fread1(&per_read_bytes, sizeof(uint16_t), f)) {
            fclose(f); return 2;
        }
        /* skip per-read header data */
        if (num_reads > 0 && per_read_bytes > 0)
            fseek(f, (long)num_reads * (long)per_read_bytes, SEEK_CUR);
    }

    /* v3..v5: lane(2)+tile(4)+cycle(2)+error_rate(4)+phix_reads[5](20) = 32 bytes
     * v6:     lane(2)+tile(4)+cycle(2)+error_rate(4)+extra(record_size-12) */
    while (!feof(f)) {
        interop_error_record_t rec;
        memset(&rec, 0, sizeof(rec));
        if (!interop__fread1(&rec.lane,       sizeof(uint16_t), f)) break;
        if (!interop__fread1(&rec.tile,       sizeof(uint32_t), f)) break;
        if (!interop__fread1(&rec.cycle,      sizeof(uint16_t), f)) break;
        if (!interop__fread1(&rec.error_rate, sizeof(float),    f)) break;
        if (version <= 5) {
            if (fread(rec.phix_read_count, sizeof(uint32_t),
                      INTEROP_NUM_ERROR_CLASSES, f) !=
                    INTEROP_NUM_ERROR_CLASSES) break;
        } else if (record_size > (uint8_t)(sizeof(uint16_t) + sizeof(uint32_t) +
                                            sizeof(uint16_t) + sizeof(float))) {
            /* skip per-read error-rate fields added in v6
             * (already read lane(2)+tile(4)+cycle(2)+error_rate(4) = 12 bytes) */
            fseek(f, (long)record_size -
                      (long)(sizeof(uint16_t) + sizeof(uint32_t) +
                             sizeof(uint16_t) + sizeof(float)), SEEK_CUR);
        }
        if (!interop__ensure_cap((void **)&out->records, out->count,
                                 &out->capacity,
                                 sizeof(interop_error_record_t))) break;
        out->records[out->count++] = rec;
    }
    fclose(f);
    return 0;
}

void interop_free_error_metrics(interop_error_metrics_t *m)
{
    if (m) { free(m->records); memset(m, 0, sizeof(*m)); }
}

/* ----------------------------------------------------------
 * Extraction Metrics parser
 * ---------------------------------------------------------- */

int interop_read_extraction_metrics(const char *filename,
                                    interop_extraction_metrics_t *out)
{
    FILE *f;
    uint8_t version, record_size;

    memset(out, 0, sizeof(*out));
    f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "interop: cannot open '%s'\n", filename);
        return 1;
    }
    if (!interop__fread1(&version,     1, f) ||
        !interop__fread1(&record_size, 1, f)) {
        fclose(f); return 2;
    }
    out->version = version;

    /* v2: lane(2)+tile(4)+cycle(2)+intensity[4](8)+fwhm[4](16)+datetime(8) = 40 bytes
     * v3: lane(2)+tile(4)+cycle(2)+intensity[4](8)+fwhm[4](16)             = 32 bytes */
    while (!feof(f)) {
        interop_extraction_record_t rec;
        memset(&rec, 0, sizeof(rec));
        if (!interop__fread1(&rec.lane,  sizeof(uint16_t), f)) break;
        if (!interop__fread1(&rec.tile,  sizeof(uint32_t), f)) break;
        if (!interop__fread1(&rec.cycle, sizeof(uint16_t), f)) break;
        if (fread(rec.intensity, sizeof(int16_t), 4, f) != 4) break;
        if (fread(rec.fwhm,      sizeof(float),   4, f) != 4) break;
        if (version == 2) {
            uint64_t datetime;
            if (!interop__fread1(&datetime, sizeof(uint64_t), f)) break;
        }
        if (!interop__ensure_cap((void **)&out->records, out->count,
                                 &out->capacity,
                                 sizeof(interop_extraction_record_t))) break;
        out->records[out->count++] = rec;
    }
    fclose(f);
    return 0;
}

void interop_free_extraction_metrics(interop_extraction_metrics_t *m)
{
    if (m) { free(m->records); memset(m, 0, sizeof(*m)); }
}

/* ----------------------------------------------------------
 * Summary Run Metrics parser
 * ---------------------------------------------------------- */

int interop_read_summary_run(const char *filename, interop_summary_run_metrics_t *out)
{
    FILE *f;
    uint8_t version;

    memset(out, 0, sizeof(*out));
    f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "interop: cannot open '%s'\n", filename);
        return 1;
    }
    if (!interop__fread1(&version, 1, f)) { fclose(f); return 2; }
    out->version = version;

    while (!feof(f)) {
        interop_summary_run_record_t rec;
        int16_t dummy;
        int32_t size;
        if (!interop__fread1(&dummy, sizeof(int16_t), f)) break;
        if (!interop__fread1(&size,  sizeof(int32_t), f)) break;
        if (!interop__fread1(&rec.occupancy_proxy_cluster_count, sizeof(double), f)) break;
        if (!interop__fread1(&rec.raw_cluster_count,             sizeof(double), f)) break;
        if (!interop__fread1(&rec.occupancy_cluster_count,       sizeof(double), f)) break;
        if (!interop__fread1(&rec.pf_cluster_count,              sizeof(double), f)) break;
        if (!interop__ensure_cap((void **)&out->records, out->count,
                                 &out->capacity,
                                 sizeof(interop_summary_run_record_t))) break;
        out->records[out->count++] = rec;
    }
    fclose(f);
    return 0;
}

void interop_free_summary_run(interop_summary_run_metrics_t *m)
{
    if (m) { free(m->records); memset(m, 0, sizeof(*m)); }
}

/* ----------------------------------------------------------
 * Extended Tile Metrics parser
 * ---------------------------------------------------------- */

int interop_read_extended_tile_metrics(const char *filename,
                                       interop_extended_tile_metrics_t *out)
{
    FILE *f;
    uint8_t version, record_size;

    memset(out, 0, sizeof(*out));
    f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "interop: cannot open '%s'\n", filename);
        return 1;
    }
    if (!interop__fread1(&version,     1, f) ||
        !interop__fread1(&record_size, 1, f)) {
        fclose(f); return 2;
    }
    out->version = version;

    /* v1: lane(2)+tile(4)+occupied(4) = 10 bytes
     * v2: above + upper_pct_base_occupied(4) = 14 bytes
     * v3: above + additional(4) = 18 bytes */
    while (!feof(f)) {
        interop_extended_tile_record_t rec;
        if (!interop__fread1(&rec.lane,  sizeof(uint16_t), f)) break;
        if (!interop__fread1(&rec.tile,  sizeof(uint32_t), f)) break;
        if (!interop__fread1(&rec.cluster_count_occupied, sizeof(float), f)) break;
        /* Skip extra fields present in v2 and v3 */
        if (version >= 2 && record_size >= 14) {
            float tmp;
            if (!interop__fread1(&tmp, sizeof(float), f)) break;
        }
        if (version >= 3 && record_size >= 18) {
            uint32_t tmp;
            if (!interop__fread1(&tmp, sizeof(uint32_t), f)) break;
        }
        if (!interop__ensure_cap((void **)&out->records, out->count,
                                 &out->capacity,
                                 sizeof(interop_extended_tile_record_t))) break;
        out->records[out->count++] = rec;
    }
    fclose(f);
    return 0;
}

void interop_free_extended_tile_metrics(interop_extended_tile_metrics_t *m)
{
    if (m) { free(m->records); memset(m, 0, sizeof(*m)); }
}

/* ----------------------------------------------------------
 * Aggregate statistics
 * ---------------------------------------------------------- */

uint64_t interop_compute_total_reads(const interop_qmetrics_t *m)
{
    uint64_t total = 0;
    size_t i, j;
    for (i = 0; i < m->count; i++)
        for (j = 0; j < m->records[i].num_bins; j++)
            total += m->records[i].histogram[j];
    return total;
}

uint64_t interop_compute_q30_reads(const interop_qmetrics_t *m)
{
    uint64_t q30 = 0;
    size_t i, j;
    for (i = 0; i < m->count; i++)
        for (j = 0; j < m->records[i].num_bins; j++)
            if (m->bin_value[j] >= 30)
                q30 += m->records[i].histogram[j];
    return q30;
}

double interop_compute_percent_q30(const interop_qmetrics_t *m)
{
    uint64_t total = interop_compute_total_reads(m);
    uint64_t q30   = interop_compute_q30_reads(m);
    return (total > 0) ? (double)q30 / (double)total * 100.0 : 0.0;
}

double interop_compute_avg_error_rate(const interop_error_metrics_t *m, int lane)
{
    double sum = 0.0;
    size_t n = 0, i;
    for (i = 0; i < m->count; i++) {
        if (lane < 0 || m->records[i].lane == (uint16_t)lane) {
            sum += m->records[i].error_rate;
            n++;
        }
    }
    return n ? sum / (double)n : 0.0;
}

double interop_compute_intensity_cycle1(const interop_extraction_metrics_t *m,
                                        int channel, int lane)
{
    double sum = 0.0;
    size_t n = 0, i;
    for (i = 0; i < m->count; i++) {
        const interop_extraction_record_t *r = &m->records[i];
        if (r->cycle == 1 && (lane < 0 || r->lane == (uint16_t)lane)) {
            sum += r->intensity[channel];
            n++;
        }
    }
    return n ? sum / (double)n : 0.0;
}

/* ----------------------------------------------------------
 * Human-readable print functions
 * ---------------------------------------------------------- */

void interop_print_qmetrics(const interop_qmetrics_t *m)
{
    size_t i, j;
    printf("Q-Metrics Version: %u\n", m->version);
    printf("Number of bins: %u\n", m->num_bins);
    for (i = 0; i < m->num_bins; i++)
        printf("  Bin %2zu: low=%3u  high=%3u  value=Q%u\n",
               i + 1, m->bin_low[i], m->bin_high[i], m->bin_value[i]);
    printf("Records: %zu\n", m->count);
    for (i = 0; i < m->count; i++) {
        const interop_qmetric_record_t *r = &m->records[i];
        uint64_t total = 0, q30 = 0;
        printf("Lane: %u, Tile: %u, Cycle: %u\n  Histogram:",
               r->lane, r->tile, r->cycle);
        for (j = 0; j < r->num_bins; j++) {
            printf(" %u", r->histogram[j]);
            total += r->histogram[j];
            if (m->bin_value[j] >= 30) q30 += r->histogram[j];
        }
        printf("\n  Total >= Q30: %llu  (%.2f%%)\n",
               (unsigned long long)q30,
               total > 0 ? (double)q30 / (double)total * 100.0 : 0.0);
    }
}

void interop_print_tile_metrics(const interop_tile_metrics_t *m)
{
    size_t i;
    printf("Tile Metrics Version: %u\n", m->version);
    printf("Records: %zu\n", m->count);
    for (i = 0; i < m->count; i++) {
        const interop_tile_record_t *r = &m->records[i];
        printf("Lane: %u, Tile: %u, Code: %u, Value: %.4f\n",
               r->lane, r->tile, r->code, r->value);
    }
}

void interop_print_corrected_int_metrics(const interop_corrected_int_metrics_t *m)
{
    size_t i;
    printf("Corrected Intensity Metrics Version: %u\n", m->version);
    printf("Records: %zu\n", m->count);
    for (i = 0; i < m->count; i++) {
        const interop_corrected_int_record_t *r = &m->records[i];
        printf("Lane: %u, Tile: %u, Cycle: %u\n", r->lane, r->tile, r->cycle);
        printf("  No Call: %u  A: %u  C: %u  G: %u  T: %u\n",
               r->no_call,
               r->base_count[0], r->base_count[1],
               r->base_count[2], r->base_count[3]);
    }
}

void interop_print_index_metrics(const interop_index_metrics_t *m)
{
    size_t i;
    printf("Index Metrics Version: %u\n", m->version);
    printf("Records: %zu\n", m->count);
    for (i = 0; i < m->count; i++) {
        const interop_index_record_t *r = &m->records[i];
        printf("Lane: %u, Tile: %u, Read: %u\n", r->lane, r->tile, r->read);
        printf("  Index: %s\n  Sample: %s\n  Project: %s\n  Clusters: %llu\n",
               r->index_name, r->sample_name, r->project_name,
               (unsigned long long)r->cluster_count);
    }
}

void interop_print_error_metrics(const interop_error_metrics_t *m)
{
    size_t i;
    printf("Error Metrics Version: %u\n", m->version);
    printf("Records: %zu\n", m->count);
    for (i = 0; i < m->count; i++) {
        const interop_error_record_t *r = &m->records[i];
        printf("Lane: %u, Tile: %u, Cycle: %u, Error Rate: %.4f%%\n",
               r->lane, r->tile, r->cycle, r->error_rate);
        if (m->version <= 5)
            printf("  PhiX reads (0-4 errors): %u %u %u %u %u\n",
                   r->phix_read_count[0], r->phix_read_count[1],
                   r->phix_read_count[2], r->phix_read_count[3],
                   r->phix_read_count[4]);
    }
}

void interop_print_extraction_metrics(const interop_extraction_metrics_t *m)
{
    size_t i;
    printf("Extraction Metrics Version: %u\n", m->version);
    printf("Records: %zu\n", m->count);
    for (i = 0; i < m->count; i++) {
        const interop_extraction_record_t *r = &m->records[i];
        printf("Lane: %u, Tile: %u, Cycle: %u\n", r->lane, r->tile, r->cycle);
        printf("  Intensity (A,C,G,T): %d, %d, %d, %d\n",
               r->intensity[0], r->intensity[1],
               r->intensity[2], r->intensity[3]);
        printf("  FWHM (A,C,G,T): %.2f, %.2f, %.2f, %.2f\n",
               r->fwhm[0], r->fwhm[1], r->fwhm[2], r->fwhm[3]);
    }
}

void interop_print_summary_run(const interop_summary_run_metrics_t *m)
{
    size_t i;
    printf("Summary Run Metrics Version: %u\n", m->version);
    printf("Records: %zu\n", m->count);
    for (i = 0; i < m->count; i++) {
        const interop_summary_run_record_t *r = &m->records[i];
        double pct_pf  = r->raw_cluster_count > 0
                       ? r->pf_cluster_count / r->raw_cluster_count * 100.0 : 0.0;
        double pct_occ = r->raw_cluster_count > 0
                       ? r->occupancy_cluster_count / r->raw_cluster_count * 100.0 : 0.0;
        printf("  Occupancy Proxy Cluster Count: %.5e\n",
               r->occupancy_proxy_cluster_count);
        printf("  Raw Cluster Count:             %.5e\n", r->raw_cluster_count);
        printf("  Occupancy Cluster Count:       %.5e\n", r->occupancy_cluster_count);
        printf("  PF Cluster Count:              %.5e\n", r->pf_cluster_count);
        printf("  Estimated PF Tbases (302 bp):  %.1f Tbases\n",
               r->pf_cluster_count * 302.0 / 1e12);
        printf("  %% PF:       %.2f%%\n", pct_pf);
        printf("  %% Occupied: %.2f%%\n", pct_occ);
    }
}

void interop_print_extended_tile_metrics(const interop_extended_tile_metrics_t *m)
{
    size_t i;
    printf("Extended Tile Metrics Version: %u\n", m->version);
    printf("Records: %zu\n", m->count);
    for (i = 0; i < m->count; i++) {
        const interop_extended_tile_record_t *r = &m->records[i];
        printf("Lane: %u, Tile: %u, Occupied Clusters: %.2f\n",
               r->lane, r->tile, r->cluster_count_occupied);
    }
}

/* ----------------------------------------------------------
 * CSV dump functions
 * ---------------------------------------------------------- */

void interop_dump_qmetrics_csv(const interop_qmetrics_t *m, FILE *out)
{
    size_t i, j;
    fprintf(out, "Lane,Tile,Cycle");
    for (j = 0; j < m->num_bins; j++)
        fprintf(out, ",Q%u", m->bin_value[j]);
    fprintf(out, "\n");
    for (i = 0; i < m->count; i++) {
        const interop_qmetric_record_t *r = &m->records[i];
        fprintf(out, "%u,%u,%u", r->lane, r->tile, r->cycle);
        for (j = 0; j < r->num_bins; j++)
            fprintf(out, ",%u", r->histogram[j]);
        fprintf(out, "\n");
    }
}

void interop_dump_tile_metrics_csv(const interop_tile_metrics_t *m, FILE *out)
{
    size_t i;
    fprintf(out, "Lane,Tile,Code,Value\n");
    for (i = 0; i < m->count; i++) {
        const interop_tile_record_t *r = &m->records[i];
        fprintf(out, "%u,%u,%u,%.4f\n", r->lane, r->tile, r->code, r->value);
    }
}

void interop_dump_corrected_int_metrics_csv(const interop_corrected_int_metrics_t *m,
                                            FILE *out)
{
    size_t i;
    fprintf(out, "Lane,Tile,Cycle,NoCall,A,C,G,T\n");
    for (i = 0; i < m->count; i++) {
        const interop_corrected_int_record_t *r = &m->records[i];
        fprintf(out, "%u,%u,%u,%u,%u,%u,%u,%u\n",
                r->lane, r->tile, r->cycle, r->no_call,
                r->base_count[0], r->base_count[1],
                r->base_count[2], r->base_count[3]);
    }
}

void interop_dump_index_metrics_csv(const interop_index_metrics_t *m, FILE *out)
{
    size_t i;
    fprintf(out, "Lane,Tile,Read,IndexName,ClusterCount,SampleName,ProjectName\n");
    for (i = 0; i < m->count; i++) {
        const interop_index_record_t *r = &m->records[i];
        fprintf(out, "%u,%u,%u,\"%s\",%llu,\"%s\",\"%s\"\n",
                r->lane, r->tile, r->read, r->index_name,
                (unsigned long long)r->cluster_count,
                r->sample_name, r->project_name);
    }
}

void interop_dump_error_metrics_csv(const interop_error_metrics_t *m, FILE *out)
{
    size_t i;
    fprintf(out, "Lane,Tile,Cycle,ErrorRate,Reads0Err,Reads1Err,"
                 "Reads2Err,Reads3Err,Reads4Err\n");
    for (i = 0; i < m->count; i++) {
        const interop_error_record_t *r = &m->records[i];
        fprintf(out, "%u,%u,%u,%.4f,%u,%u,%u,%u,%u\n",
                r->lane, r->tile, r->cycle, r->error_rate,
                r->phix_read_count[0], r->phix_read_count[1],
                r->phix_read_count[2], r->phix_read_count[3],
                r->phix_read_count[4]);
    }
}

void interop_dump_extraction_metrics_csv(const interop_extraction_metrics_t *m,
                                         FILE *out)
{
    size_t i;
    fprintf(out, "Lane,Tile,Cycle,"
                 "IntensityA,IntensityC,IntensityG,IntensityT,"
                 "FWHM_A,FWHM_C,FWHM_G,FWHM_T\n");
    for (i = 0; i < m->count; i++) {
        const interop_extraction_record_t *r = &m->records[i];
        fprintf(out, "%u,%u,%u,%d,%d,%d,%d,%.3f,%.3f,%.3f,%.3f\n",
                r->lane, r->tile, r->cycle,
                r->intensity[0], r->intensity[1],
                r->intensity[2], r->intensity[3],
                r->fwhm[0], r->fwhm[1], r->fwhm[2], r->fwhm[3]);
    }
}

void interop_dump_summary_run_csv(const interop_summary_run_metrics_t *m, FILE *out)
{
    size_t i;
    fprintf(out, "OccupancyProxy,RawClusters,OccupancyClusters,"
                 "PFClusters,PctPF,PctOccupied,EstPFTbases\n");
    for (i = 0; i < m->count; i++) {
        const interop_summary_run_record_t *r = &m->records[i];
        double pct_pf  = r->raw_cluster_count > 0
                       ? r->pf_cluster_count / r->raw_cluster_count * 100.0 : 0.0;
        double pct_occ = r->raw_cluster_count > 0
                       ? r->occupancy_cluster_count / r->raw_cluster_count * 100.0 : 0.0;
        fprintf(out, "%.5e,%.5e,%.5e,%.5e,%.2f,%.2f,%.4f\n",
                r->occupancy_proxy_cluster_count, r->raw_cluster_count,
                r->occupancy_cluster_count, r->pf_cluster_count,
                pct_pf, pct_occ,
                r->pf_cluster_count * 302.0 / 1e12);
    }
}

void interop_dump_extended_tile_metrics_csv(const interop_extended_tile_metrics_t *m,
                                            FILE *out)
{
    size_t i;
    fprintf(out, "Lane,Tile,OccupiedClusters\n");
    for (i = 0; i < m->count; i++) {
        const interop_extended_tile_record_t *r = &m->records[i];
        fprintf(out, "%u,%u,%.2f\n", r->lane, r->tile, r->cluster_count_occupied);
    }
}

/* ----------------------------------------------------------
 * Internal helper: collect unique sorted lane numbers
 * Returns the number of unique lanes found (max 256).
 * ---------------------------------------------------------- */
static int interop__collect_lanes(const uint16_t *lane_arr, size_t count,
                                  uint16_t *lanes_out, int max_lanes)
{
    int nlanes = 0, i, j, a, b;
    for (i = 0; i < (int)count; i++) {
        uint16_t lane = lane_arr[i];
        int found = 0;
        for (j = 0; j < nlanes; j++)
            if (lanes_out[j] == lane) { found = 1; break; }
        if (!found && nlanes < max_lanes)
            lanes_out[nlanes++] = lane;
    }
    for (a = 0; a < nlanes - 1; a++)
        for (b = a + 1; b < nlanes; b++)
            if (lanes_out[a] > lanes_out[b]) {
                uint16_t t = lanes_out[a]; lanes_out[a] = lanes_out[b]; lanes_out[b] = t;
            }
    return nlanes;
}

/* Wrapper to extract lane array from tile records */
static int interop__tile_lanes(const interop_tile_metrics_t *tm,
                               uint16_t *lanes, int max)
{
    /* Build a temporary lane array from tile records */
    uint16_t *tmp = (uint16_t *)malloc(tm->count * sizeof(uint16_t));
    int n = 0;
    if (!tmp) return 0;
    for (size_t i = 0; i < tm->count; i++) tmp[i] = tm->records[i].lane;
    n = interop__collect_lanes(tmp, tm->count, lanes, max);
    free(tmp);
    return n;
}

/* ----------------------------------------------------------
 * interop_print_run_summary  (SAV Summary Tab equivalent)
 * ---------------------------------------------------------- */

void interop_print_run_summary(const char *run_folder, FILE *out)
{
    char path[4096];
    interop_qmetrics_t           qm  = {0};
    interop_tile_metrics_t       tm  = {0};
    interop_error_metrics_t      em  = {0};
    interop_extraction_metrics_t xm  = {0};
    interop_summary_run_metrics_t srm = {0};

    if (interop_build_filepath(run_folder, "QMetrics",          path, sizeof(path)))
        interop_read_qmetrics(path, &qm);
    if (interop_build_filepath(run_folder, "TileMetrics",       path, sizeof(path)))
        interop_read_tile_metrics(path, &tm);
    if (interop_build_filepath(run_folder, "ErrorMetrics",      path, sizeof(path)))
        interop_read_error_metrics(path, &em);
    if (interop_build_filepath(run_folder, "ExtractionMetrics", path, sizeof(path)))
        interop_read_extraction_metrics(path, &xm);
    if (interop_build_filepath(run_folder, "SummaryRunMetrics", path, sizeof(path)))
        interop_read_summary_run(path, &srm);

    fprintf(out, "=== InterOp Run Summary ===\n\n");

    /* Q-score summary */
    if (qm.count > 0) {
        uint64_t total = interop_compute_total_reads(&qm);
        uint64_t q30   = interop_compute_q30_reads(&qm);
        fprintf(out, "Q-Score Summary:\n");
        fprintf(out, "  Total Reads : %llu\n", (unsigned long long)total);
        fprintf(out, "  Q30 Reads   : %llu\n", (unsigned long long)q30);
        fprintf(out, "  %% >= Q30    : %.2f%%\n\n",
                total > 0 ? (double)q30 / (double)total * 100.0 : 0.0);
    }

    /* Per-lane cluster stats from TileMetrics */
    if (tm.count > 0) {
        uint16_t lanes[256];
        int nlanes = interop__tile_lanes(&tm, lanes, 256);
        int k;
        fprintf(out, "Tile Metrics by Lane:\n");
        fprintf(out, "  %-6s  %-16s  %-16s  %s\n",
                "Lane", "Cluster Count", "Cluster Count PF", "% PF");
        for (k = 0; k < nlanes; k++) {
            uint16_t lane = lanes[k];
            double cc_sum = 0.0, pf_sum = 0.0;
            size_t cc_n = 0, pf_n = 0, i;
            for (i = 0; i < tm.count; i++) {
                if (tm.records[i].lane == lane) {
                    if (tm.records[i].code == INTEROP_TILE_CLUSTER_COUNT) {
                        cc_sum += tm.records[i].value; cc_n++;
                    }
                    if (tm.records[i].code == INTEROP_TILE_CLUSTER_COUNT_PF) {
                        pf_sum += tm.records[i].value; pf_n++;
                    }
                }
            }
            double cc_avg = cc_n ? cc_sum / (double)cc_n : 0.0;
            double pf_avg = pf_n ? pf_sum / (double)pf_n : 0.0;
            double pct_pf = cc_avg > 0.0 ? pf_avg / cc_avg * 100.0 : 0.0;
            fprintf(out, "  %-6u  %-16.1f  %-16.1f  %.2f%%\n",
                    lane, cc_avg, pf_avg, pct_pf);
        }
        fprintf(out, "\n");
    }

    /* Error rate summary */
    if (em.count > 0) {
        fprintf(out, "Error Rate Summary:\n");
        fprintf(out, "  Average Error Rate: %.4f%%\n\n",
                interop_compute_avg_error_rate(&em, -1));
    }

    /* Intensity at cycle 1 */
    if (xm.count > 0) {
        static const char * const ch_names[4] = {"A", "C", "G", "T"};
        int ch;
        fprintf(out, "Intensity at Cycle 1:\n");
        for (ch = 0; ch < 4; ch++)
            fprintf(out, "  Channel %s: %.1f\n", ch_names[ch],
                    interop_compute_intensity_cycle1(&xm, ch, -1));
        fprintf(out, "\n");
    }

    /* Summary run metrics */
    if (srm.count > 0) {
        const interop_summary_run_record_t *r = &srm.records[0];
        double pct_pf = r->raw_cluster_count > 0
                      ? r->pf_cluster_count / r->raw_cluster_count * 100.0 : 0.0;
        fprintf(out, "Summary Run Metrics:\n");
        fprintf(out, "  Raw Clusters   : %.5e\n", r->raw_cluster_count);
        fprintf(out, "  PF Clusters    : %.5e\n", r->pf_cluster_count);
        fprintf(out, "  %% PF           : %.2f%%\n", pct_pf);
        fprintf(out, "  Est. PF Tbases : %.2f Tbases\n\n",
                r->pf_cluster_count * 302.0 / 1e12);
    }

    interop_free_qmetrics(&qm);
    interop_free_tile_metrics(&tm);
    interop_free_error_metrics(&em);
    interop_free_extraction_metrics(&xm);
    interop_free_summary_run(&srm);
}

/* ----------------------------------------------------------
 * interop_print_imaging_table_csv  (SAV Imaging Tab equivalent)
 * ---------------------------------------------------------- */

void interop_print_imaging_table_csv(const char *run_folder, FILE *out)
{
    char path[4096];
    interop_extraction_metrics_t     xm = {0};
    interop_error_metrics_t          em = {0};
    interop_corrected_int_metrics_t  cm = {0};

    if (interop_build_filepath(run_folder, "ExtractionMetrics",    path, sizeof(path)))
        interop_read_extraction_metrics(path, &xm);
    if (interop_build_filepath(run_folder, "ErrorMetrics",         path, sizeof(path)))
        interop_read_error_metrics(path, &em);
    if (interop_build_filepath(run_folder, "CorrectedIntMetrics",  path, sizeof(path)))
        interop_read_corrected_int_metrics(path, &cm);

    fprintf(out, "Lane,Tile,Cycle,"
                 "IntensityA,IntensityC,IntensityG,IntensityT,"
                 "FWHM_A,FWHM_C,FWHM_G,FWHM_T,"
                 "ErrorRate,"
                 "NoCall,BaseA,BaseC,BaseG,BaseT\n");

    size_t i;
    for (i = 0; i < xm.count; i++) {
        const interop_extraction_record_t *r = &xm.records[i];
        /* Look up matching error rate */
        float err = 0.0f;
        size_t j;
        for (j = 0; j < em.count; j++) {
            const interop_error_record_t *e = &em.records[j];
            if (e->lane == r->lane && e->tile == r->tile && e->cycle == r->cycle) {
                err = e->error_rate; break;
            }
        }
        /* Look up matching corrected intensity */
        uint32_t no_call = 0, bc[4] = {0, 0, 0, 0};
        for (j = 0; j < cm.count; j++) {
            const interop_corrected_int_record_t *c = &cm.records[j];
            if (c->lane == r->lane && c->tile == r->tile && c->cycle == r->cycle) {
                no_call = c->no_call;
                bc[0] = c->base_count[0]; bc[1] = c->base_count[1];
                bc[2] = c->base_count[2]; bc[3] = c->base_count[3];
                break;
            }
        }
        fprintf(out, "%u,%u,%u,%d,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.4f,%u,%u,%u,%u,%u\n",
                r->lane, r->tile, r->cycle,
                r->intensity[0], r->intensity[1], r->intensity[2], r->intensity[3],
                r->fwhm[0], r->fwhm[1], r->fwhm[2], r->fwhm[3],
                err, no_call, bc[0], bc[1], bc[2], bc[3]);
    }

    interop_free_extraction_metrics(&xm);
    interop_free_error_metrics(&em);
    interop_free_corrected_int_metrics(&cm);
}

/* ----------------------------------------------------------
 * interop_print_index_summary_csv  (SAV Indexing Tab equivalent)
 * ---------------------------------------------------------- */

/* Simple per-sample aggregation struct used only inside this function */
typedef struct {
    char     sample [INTEROP_MAX_NAME_LEN];
    char     index  [INTEROP_MAX_NAME_LEN];
    char     project[INTEROP_MAX_NAME_LEN];
    uint16_t lane;
    uint64_t count;
} interop__sample_stat_t;

void interop_print_index_summary_csv(const char *run_folder, FILE *out)
{
    char path[4096];
    interop_index_metrics_t im = {0};

    if (!interop_build_filepath(run_folder, "IndexMetrics", path, sizeof(path))) {
        fprintf(stderr, "interop: IndexMetricsOut.bin not found in '%s'\n",
                run_folder);
        return;
    }
    if (interop_read_index_metrics(path, &im) != 0) return;

    interop__sample_stat_t *stats = NULL;
    size_t nstats = 0, stat_cap = 0;
    uint64_t total_count = 0;
    size_t i;

    for (i = 0; i < im.count; i++) {
        const interop_index_record_t *r = &im.records[i];
        int found = 0;
        size_t j;
        for (j = 0; j < nstats; j++) {
            if (stats[j].lane == r->lane &&
                strcmp(stats[j].sample, r->sample_name) == 0 &&
                strcmp(stats[j].index,  r->index_name)  == 0) {
                stats[j].count += r->cluster_count;
                found = 1; break;
            }
        }
        if (!found) {
            if (nstats >= stat_cap) {
                size_t nc = stat_cap ? stat_cap * 2 : 64;
                interop__sample_stat_t *tmp =
                    (interop__sample_stat_t *)realloc(
                        stats, nc * sizeof(interop__sample_stat_t));
                if (!tmp) break;
                stats = tmp; stat_cap = nc;
            }
            stats[nstats].lane = r->lane;
            strncpy(stats[nstats].sample,  r->sample_name,  INTEROP_MAX_NAME_LEN - 1);
            strncpy(stats[nstats].index,   r->index_name,   INTEROP_MAX_NAME_LEN - 1);
            strncpy(stats[nstats].project, r->project_name, INTEROP_MAX_NAME_LEN - 1);
            stats[nstats].sample [INTEROP_MAX_NAME_LEN - 1] = '\0';
            stats[nstats].index  [INTEROP_MAX_NAME_LEN - 1] = '\0';
            stats[nstats].project[INTEROP_MAX_NAME_LEN - 1] = '\0';
            stats[nstats].count = r->cluster_count;
            nstats++;
        }
        total_count += r->cluster_count;
    }

    fprintf(out, "Lane,SampleID,IndexName,ProjectName,ClusterCount,FractionMapped\n");
    for (i = 0; i < nstats; i++) {
        double frac = total_count > 0
                    ? (double)stats[i].count / (double)total_count * 100.0 : 0.0;
        fprintf(out, "%u,\"%s\",\"%s\",\"%s\",%llu,%.2f%%\n",
                stats[i].lane, stats[i].sample, stats[i].index, stats[i].project,
                (unsigned long long)stats[i].count, frac);
    }

    free(stats);
    interop_free_index_metrics(&im);
}

/* ----------------------------------------------------------
 * GNUPlot output functions
 * ---------------------------------------------------------- */

void interop_write_qscore_histogram_gnuplot(const char *run_folder, FILE *out)
{
    char path[4096];
    interop_qmetrics_t qm = {0};
    size_t j;
    uint64_t histogram[INTEROP_MAX_Q_BINS];
    memset(histogram, 0, sizeof(histogram));

    if (!interop_build_filepath(run_folder, "QMetrics", path, sizeof(path))) {
        fprintf(stderr, "interop: QMetricsOut.bin not found in '%s'\n", run_folder);
        return;
    }
    if (interop_read_qmetrics(path, &qm) != 0) return;

    /* Aggregate across all lanes, tiles, and cycles */
    size_t i;
    for (i = 0; i < qm.count; i++) {
        const interop_qmetric_record_t *r = &qm.records[i];
        for (j = 0; j < r->num_bins; j++)
            histogram[j] += r->histogram[j];
    }

    fprintf(out, "# InterOp Q-score Histogram\n");
    fprintf(out, "# Run folder: %s\n", run_folder);
    fprintf(out, "set title 'Q-score Histogram'\n");
    fprintf(out, "set xlabel 'Q-score'\n");
    fprintf(out, "set ylabel 'Read Count'\n");
    fprintf(out, "set style fill solid 0.5\n");
    fprintf(out, "set boxwidth 0.9 relative\n");
    fprintf(out, "plot '-' using 1:2 with boxes lc rgb 'blue' title 'All Cycles'\n");
    for (j = 0; j < qm.num_bins; j++)
        fprintf(out, "%u %llu\n", qm.bin_value[j], (unsigned long long)histogram[j]);
    fprintf(out, "e\n");

    interop_free_qmetrics(&qm);
}

void interop_write_qscore_heatmap_gnuplot(const char *run_folder, FILE *out)
{
    char path[4096];
    interop_qmetrics_t qm = {0};

    if (!interop_build_filepath(run_folder, "QMetrics", path, sizeof(path))) {
        fprintf(stderr, "interop: QMetricsOut.bin not found in '%s'\n", run_folder);
        return;
    }
    if (interop_read_qmetrics(path, &qm) != 0) return;

    fprintf(out, "# InterOp Q-score Heatmap\n");
    fprintf(out, "# Run folder: %s\n", run_folder);
    fprintf(out, "# Data: cycle  qscore  count\n");
    fprintf(out, "set title 'Q-score Heatmap'\n");
    fprintf(out, "set xlabel 'Cycle'\n");
    fprintf(out, "set ylabel 'Q-score'\n");
    fprintf(out, "set pm3d map\n");
    fprintf(out, "splot '-' using 1:2:3 with pm3d title ''\n");

    size_t i, j;
    for (i = 0; i < qm.count; i++) {
        const interop_qmetric_record_t *r = &qm.records[i];
        for (j = 0; j < r->num_bins; j++) {
            if (r->histogram[j] > 0)
                fprintf(out, "%u %u %u\n",
                        r->cycle, qm.bin_value[j], r->histogram[j]);
        }
    }
    fprintf(out, "e\n");

    interop_free_qmetrics(&qm);
}

void interop_write_plot_by_cycle_gnuplot(const char *run_folder, FILE *out)
{
    char path[4096];
    interop_extraction_metrics_t xm = {0};
    size_t i;
    uint16_t max_cycle = 0;

    if (!interop_build_filepath(run_folder, "ExtractionMetrics", path, sizeof(path))) {
        fprintf(stderr, "interop: ExtractionMetricsOut.bin not found in '%s'\n",
                run_folder);
        return;
    }
    if (interop_read_extraction_metrics(path, &xm) != 0) return;

    for (i = 0; i < xm.count; i++)
        if (xm.records[i].cycle > max_cycle) max_cycle = xm.records[i].cycle;

    if (max_cycle == 0) { interop_free_extraction_metrics(&xm); return; }

    /* Accumulate mean intensity per cycle (heap-allocated) */
    double *sum_a = (double *)calloc(max_cycle + 1, sizeof(double));
    double *sum_c = (double *)calloc(max_cycle + 1, sizeof(double));
    double *sum_g = (double *)calloc(max_cycle + 1, sizeof(double));
    double *sum_t = (double *)calloc(max_cycle + 1, sizeof(double));
    size_t *cnt   = (size_t *)calloc(max_cycle + 1, sizeof(size_t));

    if (sum_a && sum_c && sum_g && sum_t && cnt) {
        for (i = 0; i < xm.count; i++) {
            uint16_t cy = xm.records[i].cycle;
            sum_a[cy] += xm.records[i].intensity[0];
            sum_c[cy] += xm.records[i].intensity[1];
            sum_g[cy] += xm.records[i].intensity[2];
            sum_t[cy] += xm.records[i].intensity[3];
            cnt[cy]++;
        }

        fprintf(out, "# InterOp Intensity by Cycle\n");
        fprintf(out, "# Run folder: %s\n", run_folder);
        fprintf(out, "set title 'Intensity by Cycle'\n");
        fprintf(out, "set xlabel 'Cycle'\n");
        fprintf(out, "set ylabel 'Intensity'\n");
        fprintf(out, "plot '-' using 1:2 with lines title 'A', \\\n");
        fprintf(out, "     '-' using 1:2 with lines title 'C', \\\n");
        fprintf(out, "     '-' using 1:2 with lines title 'G', \\\n");
        fprintf(out, "     '-' using 1:2 with lines title 'T'\n");

        uint16_t cy;
        for (cy = 1; cy <= max_cycle; cy++)
            if (cnt[cy]) fprintf(out, "%u %.1f\n", cy, sum_a[cy] / cnt[cy]);
        fprintf(out, "e\n");
        for (cy = 1; cy <= max_cycle; cy++)
            if (cnt[cy]) fprintf(out, "%u %.1f\n", cy, sum_c[cy] / cnt[cy]);
        fprintf(out, "e\n");
        for (cy = 1; cy <= max_cycle; cy++)
            if (cnt[cy]) fprintf(out, "%u %.1f\n", cy, sum_g[cy] / cnt[cy]);
        fprintf(out, "e\n");
        for (cy = 1; cy <= max_cycle; cy++)
            if (cnt[cy]) fprintf(out, "%u %.1f\n", cy, sum_t[cy] / cnt[cy]);
        fprintf(out, "e\n");
    }

    free(sum_a); free(sum_c); free(sum_g); free(sum_t); free(cnt);
    interop_free_extraction_metrics(&xm);
}

void interop_write_plot_by_lane_gnuplot(const char *run_folder, FILE *out)
{
    char path[4096];
    interop_tile_metrics_t tm = {0};

    if (!interop_build_filepath(run_folder, "TileMetrics", path, sizeof(path))) {
        fprintf(stderr, "interop: TileMetricsOut.bin not found in '%s'\n", run_folder);
        return;
    }
    if (interop_read_tile_metrics(path, &tm) != 0) return;

    uint16_t lanes[256];
    int nlanes = interop__tile_lanes(&tm, lanes, 256);
    int k;

    fprintf(out, "# InterOp Cluster Count by Lane\n");
    fprintf(out, "# Run folder: %s\n", run_folder);
    fprintf(out, "set title 'Cluster Count by Lane'\n");
    fprintf(out, "set xlabel 'Lane'\n");
    fprintf(out, "set ylabel 'Cluster Count'\n");
    fprintf(out, "plot '-' using 1:2 with linespoints title 'Total', \\\n");
    fprintf(out, "     '-' using 1:2 with linespoints title 'PF'\n");

    for (k = 0; k < nlanes; k++) {
        uint16_t lane = lanes[k];
        double sum = 0.0; size_t n = 0, i;
        for (i = 0; i < tm.count; i++)
            if (tm.records[i].lane == lane &&
                tm.records[i].code == INTEROP_TILE_CLUSTER_COUNT)
                { sum += tm.records[i].value; n++; }
        fprintf(out, "%u %.2f\n", lane, n ? sum / (double)n : 0.0);
    }
    fprintf(out, "e\n");
    for (k = 0; k < nlanes; k++) {
        uint16_t lane = lanes[k];
        double sum = 0.0; size_t n = 0, i;
        for (i = 0; i < tm.count; i++)
            if (tm.records[i].lane == lane &&
                tm.records[i].code == INTEROP_TILE_CLUSTER_COUNT_PF)
                { sum += tm.records[i].value; n++; }
        fprintf(out, "%u %.2f\n", lane, n ? sum / (double)n : 0.0);
    }
    fprintf(out, "e\n");

    interop_free_tile_metrics(&tm);
}

void interop_write_flowcell_heatmap_gnuplot(const char *run_folder, FILE *out)
{
    char path[4096];
    interop_extraction_metrics_t xm = {0};
    size_t i;

    if (!interop_build_filepath(run_folder, "ExtractionMetrics", path, sizeof(path))) {
        fprintf(stderr, "interop: ExtractionMetricsOut.bin not found in '%s'\n",
                run_folder);
        return;
    }
    if (interop_read_extraction_metrics(path, &xm) != 0) return;

    fprintf(out, "# InterOp Flowcell Intensity Heatmap (Channel A, Cycle 1)\n");
    fprintf(out, "# Run folder: %s\n", run_folder);
    fprintf(out, "# Data: lane  tile  intensity_A\n");
    fprintf(out, "set title 'Flowcell Intensity Heatmap (A, Cycle 1)'\n");
    fprintf(out, "set xlabel 'Tile'\n");
    fprintf(out, "set ylabel 'Lane'\n");
    fprintf(out, "set pm3d map\n");
    fprintf(out, "splot '-' using 2:1:3 with pm3d title ''\n");

    for (i = 0; i < xm.count; i++) {
        const interop_extraction_record_t *r = &xm.records[i];
        if (r->cycle == 1)
            fprintf(out, "%u %u %d\n", r->lane, r->tile, r->intensity[0]);
    }
    fprintf(out, "e\n");

    interop_free_extraction_metrics(&xm);
}

void interop_write_sample_qc_gnuplot(const char *run_folder, FILE *out)
{
    char path[4096];
    interop_index_metrics_t im = {0};
    size_t i;

    if (!interop_build_filepath(run_folder, "IndexMetrics", path, sizeof(path))) {
        fprintf(stderr, "interop: IndexMetricsOut.bin not found in '%s'\n", run_folder);
        return;
    }
    if (interop_read_index_metrics(path, &im) != 0) return;

    /* Aggregate cluster counts by sample name */
    typedef struct { char name[INTEROP_MAX_NAME_LEN]; uint64_t count; } sc_t;
    sc_t *sc = NULL;
    size_t nsc = 0, sc_cap = 0;
    uint64_t total = 0;

    for (i = 0; i < im.count; i++) {
        const interop_index_record_t *r = &im.records[i];
        int found = 0;
        size_t j;
        for (j = 0; j < nsc; j++) {
            if (strcmp(sc[j].name, r->sample_name) == 0) {
                sc[j].count += r->cluster_count; found = 1; break;
            }
        }
        if (!found) {
            if (nsc >= sc_cap) {
                size_t nc = sc_cap ? sc_cap * 2 : 32;
                sc_t *tmp = (sc_t *)realloc(sc, nc * sizeof(sc_t));
                if (!tmp) break;
                sc = tmp; sc_cap = nc;
            }
            strncpy(sc[nsc].name, r->sample_name, INTEROP_MAX_NAME_LEN - 1);
            sc[nsc].name[INTEROP_MAX_NAME_LEN - 1] = '\0';
            sc[nsc].count = r->cluster_count;
            nsc++;
        }
        total += r->cluster_count;
    }

    fprintf(out, "# InterOp Sample Index QC\n");
    fprintf(out, "# Run folder: %s\n", run_folder);
    fprintf(out, "set title 'Sample Index QC'\n");
    fprintf(out, "set xlabel 'Sample'\n");
    fprintf(out, "set ylabel 'Cluster Count'\n");
    fprintf(out, "set xtics rotate by -45\n");
    fprintf(out, "set style data histogram\n");
    fprintf(out, "set style fill solid 0.5\n");
    fprintf(out, "plot '-' using 2:xtic(1) title 'Cluster Count'\n");
    for (i = 0; i < nsc; i++)
        fprintf(out, "\"%s\" %llu\n",
                sc[i].name, (unsigned long long)sc[i].count);
    fprintf(out, "e\n");

    free(sc);
    interop_free_index_metrics(&im);
}

#endif /* INTEROP_IMPLEMENTATION */

#ifdef __cplusplus
}
#endif

#endif /* INTEROP_READER_H */
