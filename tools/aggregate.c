/*
 * aggregate — Aggregate InterOp metrics by cycle
 *
 * Equivalent to the Illumina SAV "aggregate" command-line application.
 * Reads QMetrics and ExtractionMetrics from a run folder and writes
 * per-cycle aggregated statistics (total reads, Q30 reads, mean intensity)
 * to stdout as CSV.
 *
 * Usage:
 *   aggregate <run_folder>
 *
 * Output columns:
 *   Cycle, TotalReads, Q30Reads, PctQ30,
 *   MeanIntensityA, MeanIntensityC, MeanIntensityG, MeanIntensityT
 */

#define INTEROP_IMPLEMENTATION
#include "../interop_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s <run_folder>\n\n"
            "Aggregates InterOp metrics by cycle and writes CSV to stdout.\n\n"
            "Files used from InterOp/ (when present):\n"
            "  QMetricsOut.bin\n"
            "  ExtractionMetricsOut.bin\n",
            prog);
}

int main(int argc, char *argv[])
{
    if (argc != 2) { usage(argv[0]); return 1; }

    const char *run_folder = argv[1];
    char path[4096];

    interop_qmetrics_t           qm = {0};
    interop_extraction_metrics_t xm = {0};

    if (interop_build_filepath(run_folder, "QMetrics", path, sizeof(path)))
        interop_read_qmetrics(path, &qm);
    if (interop_build_filepath(run_folder, "ExtractionMetrics", path, sizeof(path)))
        interop_read_extraction_metrics(path, &xm);

    /* Determine the maximum cycle */
    uint16_t max_cycle = 0;
    size_t i;
    for (i = 0; i < qm.count; i++)
        if (qm.records[i].cycle > max_cycle) max_cycle = qm.records[i].cycle;
    for (i = 0; i < xm.count; i++)
        if (xm.records[i].cycle > max_cycle) max_cycle = xm.records[i].cycle;

    if (max_cycle == 0) {
        fprintf(stderr, "No metric records found in '%s'\n", run_folder);
        interop_free_qmetrics(&qm);
        interop_free_extraction_metrics(&xm);
        return 1;
    }

    /* Per-cycle accumulators (heap-allocated) */
    uint64_t *total_reads = (uint64_t *)calloc(max_cycle + 1, sizeof(uint64_t));
    uint64_t *q30_reads   = (uint64_t *)calloc(max_cycle + 1, sizeof(uint64_t));
    double   *sum_a       = (double   *)calloc(max_cycle + 1, sizeof(double));
    double   *sum_c       = (double   *)calloc(max_cycle + 1, sizeof(double));
    double   *sum_g       = (double   *)calloc(max_cycle + 1, sizeof(double));
    double   *sum_t       = (double   *)calloc(max_cycle + 1, sizeof(double));
    size_t   *xm_cnt      = (size_t   *)calloc(max_cycle + 1, sizeof(size_t));

    if (!total_reads || !q30_reads || !sum_a || !sum_c || !sum_g || !sum_t || !xm_cnt) {
        fprintf(stderr, "Error: out of memory\n");
        free(total_reads); free(q30_reads);
        free(sum_a); free(sum_c); free(sum_g); free(sum_t); free(xm_cnt);
        interop_free_qmetrics(&qm);
        interop_free_extraction_metrics(&xm);
        return 1;
    }

    /* Accumulate Q-score data per cycle */
    for (i = 0; i < qm.count; i++) {
        const interop_qmetric_record_t *r = &qm.records[i];
        uint16_t cy = r->cycle;
        size_t j;
        for (j = 0; j < r->num_bins; j++) {
            total_reads[cy] += r->histogram[j];
            if (qm.bin_value[j] >= 30)
                q30_reads[cy] += r->histogram[j];
        }
    }

    /* Accumulate intensity data per cycle */
    for (i = 0; i < xm.count; i++) {
        const interop_extraction_record_t *r = &xm.records[i];
        uint16_t cy = r->cycle;
        sum_a[cy] += r->intensity[0];
        sum_c[cy] += r->intensity[1];
        sum_g[cy] += r->intensity[2];
        sum_t[cy] += r->intensity[3];
        xm_cnt[cy]++;
    }

    /* Output CSV */
    printf("Cycle,TotalReads,Q30Reads,PctQ30,"
           "MeanIntensityA,MeanIntensityC,MeanIntensityG,MeanIntensityT\n");

    uint16_t cy;
    for (cy = 1; cy <= max_cycle; cy++) {
        double pct_q30 = total_reads[cy] > 0
                       ? (double)q30_reads[cy] / (double)total_reads[cy] * 100.0
                       : 0.0;
        double ia = xm_cnt[cy] ? sum_a[cy] / (double)xm_cnt[cy] : 0.0;
        double ic = xm_cnt[cy] ? sum_c[cy] / (double)xm_cnt[cy] : 0.0;
        double ig = xm_cnt[cy] ? sum_g[cy] / (double)xm_cnt[cy] : 0.0;
        double it = xm_cnt[cy] ? sum_t[cy] / (double)xm_cnt[cy] : 0.0;
        printf("%u,%llu,%llu,%.2f,%.1f,%.1f,%.1f,%.1f\n",
               cy,
               (unsigned long long)total_reads[cy],
               (unsigned long long)q30_reads[cy],
               pct_q30, ia, ic, ig, it);
    }

    free(total_reads); free(q30_reads);
    free(sum_a); free(sum_c); free(sum_g); free(sum_t); free(xm_cnt);
    interop_free_qmetrics(&qm);
    interop_free_extraction_metrics(&xm);
    return 0;
}
