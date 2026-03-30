/*
 * json_report — Generate a JSON quality report for PostgreSQL ingestion
 *
 * Reads all available InterOp metric files from a sequencing run folder and
 * writes a structured JSON document suitable for uploading to a quality
 * PostgreSQL database.
 *
 * Usage:
 *   json_report -i <run_folder> -o <output.json>
 *
 * Files read from InterOp/ (those present will be used):
 *   QMetricsOut.bin
 *   TileMetricsOut.bin
 *   ErrorMetricsOut.bin
 *   ExtractionMetricsOut.bin
 *   IndexMetricsOut.bin
 *   SummaryRunMetricsOut.bin
 *   ExtendedTileMetricsOut.bin
 *   CorrectedIntMetricsOut.bin
 *
 * JSON output sections:
 *   generated_at              – Timestamp of report generation
 *   run_folder                – Path to the run folder
 *   summary                   – Top-level run statistics
 *   q_score_histogram         – Per-bin Q-score read counts
 *   q_score_by_cycle          – Per-cycle mean Q-score and % >= Q30
 *   error_rate_by_cycle       – Per-cycle mean PhiX error rate
 *   intensity_by_cycle        – Per-cycle mean channel intensities (A/C/G/T)
 *   fwhm_by_cycle             – Per-cycle mean FWHM per channel
 *   base_composition_by_cycle – Per-cycle base percentages and % no-call
 *   per_lane_metrics          – Per-lane aggregated statistics
 *   index_summary             – Per-sample barcode cluster counts
 *   run_metrics               – Raw run-level cluster statistics
 */

#define INTEROP_IMPLEMENTATION
#include "../interop_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================
 * Usage / argument parsing
 * ============================================================ */

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s -i <run_folder> -o <output.json>\n\n"
            "Generates a JSON quality report for a sequencing run.\n\n"
            "Options:\n"
            "  -i <run_folder>    Path to the run folder containing an InterOp/\n"
            "                     subdirectory with the metric binary files.\n"
            "  -o <output.json>   Path for the generated JSON report.\n\n"
            "Files read from InterOp/ (those present will be used):\n"
            "  QMetricsOut.bin           – Q-score histograms\n"
            "  TileMetricsOut.bin        – Cluster density and counts per tile\n"
            "  ErrorMetricsOut.bin       – PhiX error rates per cycle\n"
            "  ExtractionMetricsOut.bin  – Channel intensity and FWHM per cycle\n"
            "  IndexMetricsOut.bin       – Barcode index assignments per sample\n"
            "  SummaryRunMetricsOut.bin  – Run-level cluster summary\n"
            "  ExtendedTileMetricsOut.bin – Tile occupancy metrics\n"
            "  CorrectedIntMetricsOut.bin – Per-cycle base counts (A/C/G/T) and no-call\n",
            prog);
}

/* ============================================================
 * JSON helpers
 * ============================================================ */

/** Write a JSON-encoded string (with surrounding quotes). */
static void json_write_string(FILE *out, const char *s)
{
    fputc('"', out);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"':  fputs("\\\"", out); break;
        case '\\': fputs("\\\\", out); break;
        case '\b': fputs("\\b",  out); break;
        case '\f': fputs("\\f",  out); break;
        case '\n': fputs("\\n",  out); break;
        case '\r': fputs("\\r",  out); break;
        case '\t': fputs("\\t",  out); break;
        default:
            if (c < 0x20)
                fprintf(out, "\\u%04x", c);
            else
                fputc(c, out);
            break;
        }
    }
    fputc('"', out);
}

/* ============================================================
 * Per-lane aggregation helpers (identical to html_report)
 * ============================================================ */

#define MAX_LANES 256

typedef struct {
    uint16_t lane;
    double   cluster_count;
    double   cluster_count_pf;
    double   density;
    double   density_pf;
    size_t   n_cluster;
    size_t   n_cluster_pf;
    size_t   n_density;
    size_t   n_density_pf;
    double   occupied;
    size_t   n_occupied;
    double   error_rate_sum;
    size_t   n_error;
    double   intensity_c1[INTEROP_NUM_CHANNELS]; /* cycle-1 intensity, A C G T */
    size_t   n_intensity_c1;
} lane_stat_t;

static int lane_index(lane_stat_t *stats, int *nstats, uint16_t lane)
{
    int i;
    for (i = 0; i < *nstats; i++)
        if (stats[i].lane == lane) return i;
    if (*nstats >= MAX_LANES) return -1;
    memset(&stats[*nstats], 0, sizeof(lane_stat_t));
    stats[*nstats].lane = lane;
    return (*nstats)++;
}

/* ============================================================
 * JSON section emitters
 * ============================================================ */

/** Emit the "summary" object. */
static void emit_json_summary(FILE *out,
    uint64_t total_reads, uint64_t q30_reads, double pct_q30,
    double avg_error,
    double pf_clusters, double raw_clusters, double occupied_clusters,
    int has_srm)
{
    double pct_pf  = (raw_clusters > 0) ? pf_clusters / raw_clusters * 100.0 : 0.0;
    double pct_occ = (raw_clusters > 0) ? occupied_clusters / raw_clusters * 100.0 : 0.0;

    fputs("  \"summary\": {\n", out);
    fprintf(out, "    \"total_reads\": %llu,\n",
            (unsigned long long)total_reads);
    fprintf(out, "    \"q30_reads\": %llu,\n",
            (unsigned long long)q30_reads);
    fprintf(out, "    \"percent_q30\": %.4f,\n", pct_q30);

    if (avg_error >= 0.0)
        fprintf(out, "    \"avg_error_rate\": %.4f,\n", avg_error);
    else
        fputs("    \"avg_error_rate\": null,\n", out);

    if (has_srm && pf_clusters > 0) {
        fprintf(out, "    \"pf_clusters\": %.0f,\n",    pf_clusters);
        fprintf(out, "    \"raw_clusters\": %.0f,\n",   raw_clusters);
        fprintf(out, "    \"percent_pf\": %.4f,\n",     pct_pf);
        if (occupied_clusters > 0) {
            fprintf(out, "    \"occupied_clusters\": %.0f,\n", occupied_clusters);
            fprintf(out, "    \"percent_occupied\": %.4f\n",   pct_occ);
        } else {
            fputs("    \"occupied_clusters\": null,\n", out);
            fputs("    \"percent_occupied\": null\n", out);
        }
    } else {
        fputs("    \"pf_clusters\": null,\n", out);
        fputs("    \"raw_clusters\": null,\n", out);
        fputs("    \"percent_pf\": null,\n", out);
        fputs("    \"occupied_clusters\": null,\n", out);
        fputs("    \"percent_occupied\": null\n", out);
    }

    fputs("  },\n", out);
}

/** Emit the "q_score_histogram" array. */
static void emit_json_qscore_histogram(FILE *out,
    const interop_qmetrics_t *qm)
{
    size_t i, j;
    uint64_t histogram[INTEROP_MAX_Q_BINS];
    uint64_t total = 0;
    memset(histogram, 0, sizeof(histogram));

    for (i = 0; i < qm->count; i++) {
        const interop_qmetric_record_t *r = &qm->records[i];
        for (j = 0; j < r->num_bins; j++) {
            histogram[j] += r->histogram[j];
            total += r->histogram[j];
        }
    }

    fputs("  \"q_score_histogram\": [\n", out);
    for (j = 0; j < qm->num_bins; j++) {
        double pct = (total > 0) ? (double)histogram[j] / (double)total * 100.0 : 0.0;
        fprintf(out,
            "    {\"q_score\": %u, \"reads\": %llu, \"percent\": %.4f}%s\n",
            qm->bin_value[j],
            (unsigned long long)histogram[j],
            pct,
            (j + 1 < qm->num_bins) ? "," : "");
    }
    fputs("  ],\n", out);
}

/** Emit the "q_score_by_cycle" array. */
static void emit_json_qscore_by_cycle(FILE *out,
    const interop_qmetrics_t *qm)
{
    uint16_t max_cycle = 0;
    size_t i;
    for (i = 0; i < qm->count; i++)
        if (qm->records[i].cycle > max_cycle)
            max_cycle = qm->records[i].cycle;

    fputs("  \"q_score_by_cycle\": ", out);
    if (max_cycle == 0) {
        fputs("[],\n", out);
        return;
    }

    double   *sum_q  = (double   *)calloc(max_cycle + 1, sizeof(double));
    uint64_t *cnt_q  = (uint64_t *)calloc(max_cycle + 1, sizeof(uint64_t));
    uint64_t *q30c   = (uint64_t *)calloc(max_cycle + 1, sizeof(uint64_t));
    uint64_t *totc   = (uint64_t *)calloc(max_cycle + 1, sizeof(uint64_t));

    if (!sum_q || !cnt_q || !q30c || !totc) {
        free(sum_q); free(cnt_q); free(q30c); free(totc);
        fputs("[],\n", out);
        return;
    }

    for (i = 0; i < qm->count; i++) {
        const interop_qmetric_record_t *r = &qm->records[i];
        uint16_t cy = r->cycle;
        size_t j;
        for (j = 0; j < r->num_bins; j++) {
            sum_q[cy] += (double)qm->bin_value[j] * r->histogram[j];
            cnt_q[cy] += r->histogram[j];
            totc[cy]  += r->histogram[j];
            if (qm->bin_value[j] >= 30)
                q30c[cy] += r->histogram[j];
        }
    }

    fputs("[\n", out);
    uint16_t cy;
    for (cy = 1; cy <= max_cycle; cy++) {
        double mean_q  = (cnt_q[cy] > 0) ? sum_q[cy] / (double)cnt_q[cy] : 0.0;
        double pct_q30 = (totc[cy]  > 0) ? (double)q30c[cy] / (double)totc[cy] * 100.0 : 0.0;
        fprintf(out,
            "    {\"cycle\": %u, \"mean_q\": %.4f, \"percent_q30\": %.4f}%s\n",
            cy, mean_q, pct_q30,
            (cy < max_cycle) ? "," : "");
    }
    fputs("  ],\n", out);

    free(sum_q); free(cnt_q); free(q30c); free(totc);
}

/** Emit the "error_rate_by_cycle" array. */
static void emit_json_error_by_cycle(FILE *out,
    const interop_error_metrics_t *em)
{
    size_t i;
    uint16_t max_cycle = 0;
    for (i = 0; i < em->count; i++)
        if (em->records[i].cycle > max_cycle)
            max_cycle = em->records[i].cycle;

    fputs("  \"error_rate_by_cycle\": ", out);
    if (max_cycle == 0) {
        fputs("[],\n", out);
        return;
    }

    double *sum_err = (double *)calloc(max_cycle + 1, sizeof(double));
    size_t *cnt_err = (size_t *)calloc(max_cycle + 1, sizeof(size_t));

    if (!sum_err || !cnt_err) {
        free(sum_err); free(cnt_err);
        fputs("[],\n", out);
        return;
    }

    for (i = 0; i < em->count; i++) {
        const interop_error_record_t *r = &em->records[i];
        sum_err[r->cycle] += r->error_rate;
        cnt_err[r->cycle]++;
    }

    fputs("[\n", out);
    uint16_t cy;
    for (cy = 1; cy <= max_cycle; cy++) {
        double v = (cnt_err[cy] > 0) ? sum_err[cy] / (double)cnt_err[cy] : 0.0;
        fprintf(out,
            "    {\"cycle\": %u, \"mean_error_rate\": %.6f}%s\n",
            cy, v,
            (cy < max_cycle) ? "," : "");
    }
    fputs("  ],\n", out);

    free(sum_err); free(cnt_err);
}

/** Emit the "intensity_by_cycle" array. */
static void emit_json_intensity_by_cycle(FILE *out,
    const interop_extraction_metrics_t *xm)
{
    size_t i;
    uint16_t max_cycle = 0;
    for (i = 0; i < xm->count; i++)
        if (xm->records[i].cycle > max_cycle)
            max_cycle = xm->records[i].cycle;

    fputs("  \"intensity_by_cycle\": ", out);
    if (max_cycle == 0) {
        fputs("[],\n", out);
        return;
    }

    double *sum_a = (double *)calloc(max_cycle + 1, sizeof(double));
    double *sum_c = (double *)calloc(max_cycle + 1, sizeof(double));
    double *sum_g = (double *)calloc(max_cycle + 1, sizeof(double));
    double *sum_t = (double *)calloc(max_cycle + 1, sizeof(double));
    size_t *cnt   = (size_t *)calloc(max_cycle + 1, sizeof(size_t));

    if (!sum_a || !sum_c || !sum_g || !sum_t || !cnt) {
        free(sum_a); free(sum_c); free(sum_g); free(sum_t); free(cnt);
        fputs("[],\n", out);
        return;
    }

    for (i = 0; i < xm->count; i++) {
        const interop_extraction_record_t *r = &xm->records[i];
        uint16_t cy = r->cycle;
        sum_a[cy] += r->intensity[0];
        sum_c[cy] += r->intensity[1];
        sum_g[cy] += r->intensity[2];
        sum_t[cy] += r->intensity[3];
        cnt[cy]++;
    }

    fputs("[\n", out);
    uint16_t cy;
    for (cy = 1; cy <= max_cycle; cy++) {
        double n = cnt[cy] ? (double)cnt[cy] : 1.0;
        double va = cnt[cy] ? sum_a[cy] / n : 0.0;
        double vc = cnt[cy] ? sum_c[cy] / n : 0.0;
        double vg = cnt[cy] ? sum_g[cy] / n : 0.0;
        double vt = cnt[cy] ? sum_t[cy] / n : 0.0;
        fprintf(out,
            "    {\"cycle\": %u,"
            " \"channel_a\": %.2f, \"channel_c\": %.2f,"
            " \"channel_g\": %.2f, \"channel_t\": %.2f}%s\n",
            cy, va, vc, vg, vt,
            (cy < max_cycle) ? "," : "");
    }
    fputs("  ],\n", out);

    free(sum_a); free(sum_c); free(sum_g); free(sum_t); free(cnt);
}

/** Emit the "fwhm_by_cycle" array. */
static void emit_json_fwhm_by_cycle(FILE *out,
    const interop_extraction_metrics_t *xm)
{
    size_t i;
    uint16_t max_cycle = 0;
    for (i = 0; i < xm->count; i++)
        if (xm->records[i].cycle > max_cycle)
            max_cycle = xm->records[i].cycle;

    fputs("  \"fwhm_by_cycle\": ", out);
    if (max_cycle == 0) {
        fputs("[],\n", out);
        return;
    }

    double *sum_a = (double *)calloc(max_cycle + 1, sizeof(double));
    double *sum_c = (double *)calloc(max_cycle + 1, sizeof(double));
    double *sum_g = (double *)calloc(max_cycle + 1, sizeof(double));
    double *sum_t = (double *)calloc(max_cycle + 1, sizeof(double));
    size_t *cnt   = (size_t *)calloc(max_cycle + 1, sizeof(size_t));

    if (!sum_a || !sum_c || !sum_g || !sum_t || !cnt) {
        free(sum_a); free(sum_c); free(sum_g); free(sum_t); free(cnt);
        fputs("[],\n", out);
        return;
    }

    for (i = 0; i < xm->count; i++) {
        const interop_extraction_record_t *r = &xm->records[i];
        uint16_t cy = r->cycle;
        sum_a[cy] += r->fwhm[0];
        sum_c[cy] += r->fwhm[1];
        sum_g[cy] += r->fwhm[2];
        sum_t[cy] += r->fwhm[3];
        cnt[cy]++;
    }

    fputs("[\n", out);
    uint16_t cy;
    for (cy = 1; cy <= max_cycle; cy++) {
        double n = cnt[cy] ? (double)cnt[cy] : 1.0;
        double va = cnt[cy] ? sum_a[cy] / n : 0.0;
        double vc = cnt[cy] ? sum_c[cy] / n : 0.0;
        double vg = cnt[cy] ? sum_g[cy] / n : 0.0;
        double vt = cnt[cy] ? sum_t[cy] / n : 0.0;
        fprintf(out,
            "    {\"cycle\": %u,"
            " \"channel_a\": %.4f, \"channel_c\": %.4f,"
            " \"channel_g\": %.4f, \"channel_t\": %.4f}%s\n",
            cy, va, vc, vg, vt,
            (cy < max_cycle) ? "," : "");
    }
    fputs("  ],\n", out);

    free(sum_a); free(sum_c); free(sum_g); free(sum_t); free(cnt);
}

/** Emit the "base_composition_by_cycle" array. */
static void emit_json_base_composition(FILE *out,
    const interop_corrected_int_metrics_t *cm)
{
    size_t i;
    uint16_t max_cycle = 0;
    for (i = 0; i < cm->count; i++)
        if (cm->records[i].cycle > max_cycle)
            max_cycle = cm->records[i].cycle;

    fputs("  \"base_composition_by_cycle\": ", out);
    if (max_cycle == 0) {
        fputs("[],\n", out);
        return;
    }

    double   *sum_a     = (double   *)calloc(max_cycle + 1, sizeof(double));
    double   *sum_c     = (double   *)calloc(max_cycle + 1, sizeof(double));
    double   *sum_g     = (double   *)calloc(max_cycle + 1, sizeof(double));
    double   *sum_t     = (double   *)calloc(max_cycle + 1, sizeof(double));
    double   *sum_nc    = (double   *)calloc(max_cycle + 1, sizeof(double));
    uint64_t *sum_total = (uint64_t *)calloc(max_cycle + 1, sizeof(uint64_t));

    if (!sum_a || !sum_c || !sum_g || !sum_t || !sum_nc || !sum_total) {
        free(sum_a); free(sum_c); free(sum_g); free(sum_t);
        free(sum_nc); free(sum_total);
        fputs("[],\n", out);
        return;
    }

    for (i = 0; i < cm->count; i++) {
        const interop_corrected_int_record_t *r = &cm->records[i];
        uint16_t cy = r->cycle;
        sum_a[cy]  += r->base_count[0];
        sum_c[cy]  += r->base_count[1];
        sum_g[cy]  += r->base_count[2];
        sum_t[cy]  += r->base_count[3];
        sum_nc[cy] += r->no_call;
        sum_total[cy] += (uint64_t)r->base_count[0] + r->base_count[1]
                       + r->base_count[2] + r->base_count[3]
                       + r->no_call;
    }

    fputs("[\n", out);
    uint16_t cy;
    for (cy = 1; cy <= max_cycle; cy++) {
        double tot = sum_total[cy] > 0 ? (double)sum_total[cy] : 1.0;
        double pa  = sum_total[cy] > 0 ? sum_a[cy]  / tot * 100.0 : 0.0;
        double pc  = sum_total[cy] > 0 ? sum_c[cy]  / tot * 100.0 : 0.0;
        double pg  = sum_total[cy] > 0 ? sum_g[cy]  / tot * 100.0 : 0.0;
        double pt  = sum_total[cy] > 0 ? sum_t[cy]  / tot * 100.0 : 0.0;
        double pnc = sum_total[cy] > 0 ? sum_nc[cy] / tot * 100.0 : 0.0;
        fprintf(out,
            "    {\"cycle\": %u,"
            " \"percent_a\": %.4f, \"percent_c\": %.4f,"
            " \"percent_g\": %.4f, \"percent_t\": %.4f,"
            " \"percent_no_call\": %.4f}%s\n",
            cy, pa, pc, pg, pt, pnc,
            (cy < max_cycle) ? "," : "");
    }
    fputs("  ],\n", out);

    free(sum_a); free(sum_c); free(sum_g); free(sum_t);
    free(sum_nc); free(sum_total);
}

/** Emit the "per_lane_metrics" array. */
static void emit_json_lane_metrics(FILE *out,
    const lane_stat_t *stats, int nlanes)
{
    fputs("  \"per_lane_metrics\": [\n", out);
    int k;
    for (k = 0; k < nlanes; k++) {
        const lane_stat_t *s = &stats[k];
        double cc   = s->n_cluster    ? s->cluster_count    / (double)s->n_cluster    : 0.0;
        double ccpf = s->n_cluster_pf ? s->cluster_count_pf / (double)s->n_cluster_pf : 0.0;
        double den  = s->n_density    ? s->density          / (double)s->n_density    : 0.0;
        double denpf= s->n_density_pf ? s->density_pf       / (double)s->n_density_pf: 0.0;
        double pctpf= (cc > 0) ? ccpf / cc * 100.0 : 0.0;
        double occ  = s->n_occupied   ? s->occupied         / (double)s->n_occupied   : 0.0;
        double pctocc = (cc > 0) ? occ / cc * 100.0 : 0.0;
        double ia = s->n_intensity_c1 ? s->intensity_c1[0] / (double)s->n_intensity_c1 : 0.0;
        double ic = s->n_intensity_c1 ? s->intensity_c1[1] / (double)s->n_intensity_c1 : 0.0;
        double ig = s->n_intensity_c1 ? s->intensity_c1[2] / (double)s->n_intensity_c1 : 0.0;
        double it = s->n_intensity_c1 ? s->intensity_c1[3] / (double)s->n_intensity_c1 : 0.0;

        fprintf(out, "    {\n");
        fprintf(out, "      \"lane\": %u,\n",                s->lane);
        fprintf(out, "      \"cluster_count\": %.0f,\n",     cc);
        fprintf(out, "      \"cluster_count_pf\": %.0f,\n",  ccpf);
        fprintf(out, "      \"percent_pf\": %.4f,\n",        pctpf);
        fprintf(out, "      \"density\": %.2f,\n",           den);
        fprintf(out, "      \"density_pf\": %.2f,\n",        denpf);

        if (s->n_occupied > 0)
            fprintf(out, "      \"percent_occupied\": %.4f,\n", pctocc);
        else
            fputs("      \"percent_occupied\": null,\n", out);

        if (s->n_error > 0)
            fprintf(out, "      \"mean_error_rate\": %.6f,\n",
                    s->error_rate_sum / (double)s->n_error);
        else
            fputs("      \"mean_error_rate\": null,\n", out);

        fprintf(out, "      \"intensity_c1_a\": %.2f,\n", ia);
        fprintf(out, "      \"intensity_c1_c\": %.2f,\n", ic);
        fprintf(out, "      \"intensity_c1_g\": %.2f,\n", ig);
        fprintf(out, "      \"intensity_c1_t\": %.2f\n",  it);
        fprintf(out, "    }%s\n", (k + 1 < nlanes) ? "," : "");
    }
    fputs("  ],\n", out);
}

/** Emit the "index_summary" array. */
typedef struct {
    char     sample [INTEROP_MAX_NAME_LEN];
    char     index  [INTEROP_MAX_NAME_LEN];
    char     project[INTEROP_MAX_NAME_LEN];
    uint16_t lane;
    uint64_t count;
} json_sample_stat_t;

static void emit_json_index_summary(FILE *out,
    const interop_index_metrics_t *im)
{
    fputs("  \"index_summary\": ", out);
    if (im->count == 0) {
        fputs("[],\n", out);
        return;
    }

    json_sample_stat_t *stats = NULL;
    size_t nstats = 0, stat_cap = 0;
    uint64_t total_count = 0;
    size_t i;

    for (i = 0; i < im->count; i++) {
        const interop_index_record_t *r = &im->records[i];
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
                json_sample_stat_t *tmp =
                    (json_sample_stat_t *)realloc(stats, nc * sizeof(json_sample_stat_t));
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

    fputs("[\n", out);
    for (i = 0; i < nstats; i++) {
        double frac = (total_count > 0)
            ? (double)stats[i].count / (double)total_count * 100.0 : 0.0;
        fputs("    {\n", out);
        fprintf(out, "      \"lane\": %u,\n", stats[i].lane);
        fputs("      \"sample_name\": ", out);
        json_write_string(out, stats[i].sample);
        fputs(",\n", out);
        fputs("      \"index_name\": ", out);
        json_write_string(out, stats[i].index);
        fputs(",\n", out);
        fputs("      \"project_name\": ", out);
        json_write_string(out, stats[i].project);
        fputs(",\n", out);
        fprintf(out, "      \"cluster_count\": %llu,\n",
                (unsigned long long)stats[i].count);
        fprintf(out, "      \"percent_of_total\": %.4f\n", frac);
        fprintf(out, "    }%s\n", (i + 1 < nstats) ? "," : "");
    }
    fputs("  ],\n", out);

    free(stats);
}

/** Emit the "run_metrics" object. */
static void emit_json_run_metrics(FILE *out,
    const interop_summary_run_metrics_t *srm,
    const interop_extended_tile_metrics_t *etm,
    int has_srm, int has_etm)
{
    fputs("  \"run_metrics\": {\n", out);

    if (has_srm && srm->count > 0) {
        const interop_summary_run_record_t *r = &srm->records[0];
        double pct_pf = (r->raw_cluster_count > 0)
            ? r->pf_cluster_count / r->raw_cluster_count * 100.0 : 0.0;
        double gbases = r->pf_cluster_count * INTEROP_DEFAULT_READ_LENGTH_BP / 1e9;

        fprintf(out, "    \"raw_cluster_count\": %.0f,\n",
                r->raw_cluster_count);
        fprintf(out, "    \"pf_cluster_count\": %.0f,\n",
                r->pf_cluster_count);
        fprintf(out, "    \"occupancy_cluster_count\": %.0f,\n",
                r->occupancy_cluster_count);
        fprintf(out, "    \"occupancy_proxy_cluster_count\": %.0f,\n",
                r->occupancy_proxy_cluster_count);
        fprintf(out, "    \"percent_pf\": %.4f,\n", pct_pf);
        fprintf(out, "    \"estimated_pf_gbases\": %.4f,\n", gbases);
        fprintf(out, "    \"read_length_bp\": %d%s\n",
                INTEROP_DEFAULT_READ_LENGTH_BP,
                has_etm ? "," : "");
    } else {
        fputs("    \"raw_cluster_count\": null,\n", out);
        fputs("    \"pf_cluster_count\": null,\n", out);
        fputs("    \"occupancy_cluster_count\": null,\n", out);
        fputs("    \"occupancy_proxy_cluster_count\": null,\n", out);
        fputs("    \"percent_pf\": null,\n", out);
        fputs("    \"estimated_pf_gbases\": null,\n", out);
        fprintf(out, "    \"read_length_bp\": %d%s\n",
                INTEROP_DEFAULT_READ_LENGTH_BP,
                has_etm ? "," : "");
    }

    if (has_etm && etm->count > 0) {
        double total_occ = 0.0;
        size_t i;
        for (i = 0; i < etm->count; i++)
            total_occ += etm->records[i].cluster_count_occupied;
        fprintf(out, "    \"extended_tile_records\": %zu,\n", etm->count);
        fprintf(out, "    \"total_occupied_clusters\": %.0f\n", total_occ);
    } else if (has_etm) {
        fputs("    \"extended_tile_records\": 0,\n", out);
        fputs("    \"total_occupied_clusters\": null\n", out);
    }

    fputs("  }\n", out);
}

/* ============================================================
 * main
 * ============================================================ */

int main(int argc, char *argv[])
{
    const char *run_folder = NULL;
    const char *output_path = NULL;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 && i + 1 < argc)
            run_folder = argv[++i];
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            output_path = argv[++i];
        else if (strcmp(argv[i], "-h") == 0 ||
                 strcmp(argv[i], "--help") == 0) {
            usage(argv[0]); return 0;
        }
    }

    if (!run_folder || !output_path) {
        usage(argv[0]);
        return 1;
    }

    /* Open output file */
    FILE *out = fopen(output_path, "w");
    if (!out) {
        fprintf(stderr, "json_report: cannot open '%s' for writing\n", output_path);
        return 1;
    }

    /* Timestamp */
    char ts_buf[64];
    {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%dT%H:%M:%S", tm_info);
    }

    /* ----------------------------------------------------------
     * Load all available InterOp files
     * ---------------------------------------------------------- */
    char path[4096];
    interop_qmetrics_t              qm  = {0};
    interop_tile_metrics_t          tm  = {0};
    interop_error_metrics_t         em  = {0};
    interop_extraction_metrics_t    xm  = {0};
    interop_index_metrics_t         im  = {0};
    interop_summary_run_metrics_t   srm = {0};
    interop_extended_tile_metrics_t etm = {0};
    interop_corrected_int_metrics_t cm  = {0};

    int has_qm  = 0, has_tm  = 0, has_em  = 0, has_xm  = 0;
    int has_im  = 0, has_srm = 0, has_etm = 0, has_cm  = 0;

    if (interop_build_filepath(run_folder, "QMetrics",           path, sizeof(path)))
        has_qm  = (interop_read_qmetrics(path, &qm)                   == 0);
    if (interop_build_filepath(run_folder, "TileMetrics",         path, sizeof(path)))
        has_tm  = (interop_read_tile_metrics(path, &tm)                == 0);
    if (interop_build_filepath(run_folder, "ErrorMetrics",        path, sizeof(path)))
        has_em  = (interop_read_error_metrics(path, &em)               == 0);
    if (interop_build_filepath(run_folder, "ExtractionMetrics",   path, sizeof(path)))
        has_xm  = (interop_read_extraction_metrics(path, &xm)          == 0);
    if (interop_build_filepath(run_folder, "IndexMetrics",        path, sizeof(path)))
        has_im  = (interop_read_index_metrics(path, &im)               == 0);
    if (interop_build_filepath(run_folder, "SummaryRunMetrics",   path, sizeof(path)))
        has_srm = (interop_read_summary_run(path, &srm)                == 0);
    if (interop_build_filepath(run_folder, "ExtendedTileMetrics", path, sizeof(path)))
        has_etm = (interop_read_extended_tile_metrics(path, &etm)      == 0);
    if (interop_build_filepath(run_folder, "CorrectedIntMetrics", path, sizeof(path)))
        has_cm  = (interop_read_corrected_int_metrics(path, &cm)       == 0);

    /* ----------------------------------------------------------
     * Pre-compute per-lane statistics
     * ---------------------------------------------------------- */
    lane_stat_t lane_stats[MAX_LANES];
    int nlanes = 0;
    memset(lane_stats, 0, sizeof(lane_stats));

    if (has_tm) {
        size_t ri;
        for (ri = 0; ri < tm.count; ri++) {
            const interop_tile_record_t *r = &tm.records[ri];
            int idx = lane_index(lane_stats, &nlanes, r->lane);
            if (idx < 0) continue;
            if (r->code == INTEROP_TILE_CLUSTER_COUNT)
                { lane_stats[idx].cluster_count    += r->value; lane_stats[idx].n_cluster++;    }
            else if (r->code == INTEROP_TILE_CLUSTER_COUNT_PF)
                { lane_stats[idx].cluster_count_pf += r->value; lane_stats[idx].n_cluster_pf++; }
            else if (r->code == INTEROP_TILE_DENSITY)
                { lane_stats[idx].density          += r->value; lane_stats[idx].n_density++;    }
            else if (r->code == INTEROP_TILE_DENSITY_PF)
                { lane_stats[idx].density_pf       += r->value; lane_stats[idx].n_density_pf++; }
        }
        /* v3+: density stored in file header rather than per-tile records */
        if (tm.version >= 3 && tm.density > 0.0f) {
            int k;
            for (k = 0; k < nlanes; k++) {
                if (lane_stats[k].n_density == 0) {
                    lane_stats[k].density   = tm.density;
                    lane_stats[k].n_density = 1;
                }
            }
        }
    }

    if (has_em) {
        size_t ri;
        for (ri = 0; ri < em.count; ri++) {
            const interop_error_record_t *r = &em.records[ri];
            int idx = lane_index(lane_stats, &nlanes, r->lane);
            if (idx < 0) continue;
            lane_stats[idx].error_rate_sum += r->error_rate;
            lane_stats[idx].n_error++;
        }
    }

    if (has_xm) {
        size_t ri;
        for (ri = 0; ri < xm.count; ri++) {
            const interop_extraction_record_t *r = &xm.records[ri];
            if (r->cycle != 1) continue;
            int idx = lane_index(lane_stats, &nlanes, r->lane);
            if (idx < 0) continue;
            lane_stats[idx].intensity_c1[0] += r->intensity[0];
            lane_stats[idx].intensity_c1[1] += r->intensity[1];
            lane_stats[idx].intensity_c1[2] += r->intensity[2];
            lane_stats[idx].intensity_c1[3] += r->intensity[3];
            lane_stats[idx].n_intensity_c1++;
        }
    }

    if (has_etm) {
        size_t ri;
        for (ri = 0; ri < etm.count; ri++) {
            const interop_extended_tile_record_t *r = &etm.records[ri];
            int idx = lane_index(lane_stats, &nlanes, r->lane);
            if (idx < 0) continue;
            lane_stats[idx].occupied   += r->cluster_count_occupied;
            lane_stats[idx].n_occupied++;
        }
    }

    /* ----------------------------------------------------------
     * Compute top-level summary statistics
     * ---------------------------------------------------------- */
    uint64_t total_reads = has_qm ? interop_compute_total_reads(&qm) : 0;
    uint64_t q30_reads   = has_qm ? interop_compute_q30_reads(&qm)   : 0;
    double   pct_q30     = has_qm ? interop_compute_percent_q30(&qm)  : 0.0;
    double   avg_error   = has_em ? interop_compute_avg_error_rate(&em, -1) : -1.0;

    double pf_clusters = 0.0, raw_clusters = 0.0, occ_clusters = 0.0;
    if (has_srm && srm.count > 0) {
        pf_clusters  = srm.records[0].pf_cluster_count;
        raw_clusters = srm.records[0].raw_cluster_count;
        occ_clusters = srm.records[0].occupancy_cluster_count;
    }

    /* Fallback: sum PF clusters from tile metrics */
    if (pf_clusters == 0.0 && has_tm) {
        size_t ri;
        for (ri = 0; ri < tm.count; ri++)
            if (tm.records[ri].code == INTEROP_TILE_CLUSTER_COUNT_PF)
                pf_clusters += tm.records[ri].value;
        for (ri = 0; ri < tm.count; ri++)
            if (tm.records[ri].code == INTEROP_TILE_CLUSTER_COUNT)
                raw_clusters += tm.records[ri].value;
    }

    /* Occupied clusters from extended tile */
    if (occ_clusters == 0.0 && has_etm) {
        size_t ri;
        for (ri = 0; ri < etm.count; ri++)
            occ_clusters += etm.records[ri].cluster_count_occupied;
    }

    /* Use pf_cluster_count as the authoritative total PF read count when
     * available, deriving the Q30 read count from the percentage. */
    if (pf_clusters > 0.0) {
        total_reads = (uint64_t)pf_clusters;
        q30_reads   = (uint64_t)(pct_q30 * 0.01 * pf_clusters + 0.5);
    }

    /* ----------------------------------------------------------
     * Write JSON
     * ---------------------------------------------------------- */
    fputs("{\n", out);

    fputs("  \"generated_at\": ", out);
    json_write_string(out, ts_buf);
    fputs(",\n", out);

    fputs("  \"run_folder\": ", out);
    json_write_string(out, run_folder);
    fputs(",\n", out);

    emit_json_summary(out,
        total_reads, q30_reads, pct_q30,
        avg_error,
        pf_clusters, raw_clusters, occ_clusters,
        has_srm);

    if (has_qm) {
        emit_json_qscore_histogram(out, &qm);
        emit_json_qscore_by_cycle(out, &qm);
    } else {
        fputs("  \"q_score_histogram\": [],\n", out);
        fputs("  \"q_score_by_cycle\": [],\n", out);
    }

    if (has_em)
        emit_json_error_by_cycle(out, &em);
    else
        fputs("  \"error_rate_by_cycle\": [],\n", out);

    if (has_xm) {
        emit_json_intensity_by_cycle(out, &xm);
        emit_json_fwhm_by_cycle(out, &xm);
    } else {
        fputs("  \"intensity_by_cycle\": [],\n", out);
        fputs("  \"fwhm_by_cycle\": [],\n", out);
    }

    if (has_cm)
        emit_json_base_composition(out, &cm);
    else
        fputs("  \"base_composition_by_cycle\": [],\n", out);

    emit_json_lane_metrics(out, lane_stats, nlanes);

    if (has_im)
        emit_json_index_summary(out, &im);
    else
        fputs("  \"index_summary\": [],\n", out);

    emit_json_run_metrics(out, &srm, &etm, has_srm, has_etm);

    fputs("}\n", out);

    /* Free all metric data */
    if (has_qm)  interop_free_qmetrics(&qm);
    if (has_tm)  interop_free_tile_metrics(&tm);
    if (has_em)  interop_free_error_metrics(&em);
    if (has_xm)  interop_free_extraction_metrics(&xm);
    if (has_im)  interop_free_index_metrics(&im);
    if (has_srm) interop_free_summary_run(&srm);
    if (has_etm) interop_free_extended_tile_metrics(&etm);
    if (has_cm)  interop_free_corrected_int_metrics(&cm);

    fclose(out);

    fprintf(stderr, "json_report: report written to '%s'\n", output_path);
    return 0;
}
