/*
 * html_report — Generate an extensive HTML run report
 *
 * Reads all available InterOp metric files from a sequencing run folder and
 * writes a self-contained HTML page with interactive charts, summary cards,
 * and per-cycle / per-lane / per-sample tables.
 *
 * Usage:
 *   html_report -i <run_folder> -o <output.html>
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
            "Usage: %s -i <run_folder> -o <output.html>\n\n"
            "Generates an extensive HTML report for a sequencing run.\n\n"
            "Options:\n"
            "  -i <run_folder>   Path to the run folder containing an InterOp/\n"
            "                    subdirectory with the metric binary files.\n"
            "  -o <output.html>  Path for the generated HTML report.\n\n"
            "Files read from InterOp/ (those present will be used):\n"
            "  QMetricsOut.bin\n"
            "  TileMetricsOut.bin\n"
            "  ErrorMetricsOut.bin\n"
            "  ExtractionMetricsOut.bin\n"
            "  IndexMetricsOut.bin\n"
            "  SummaryRunMetricsOut.bin\n"
            "  ExtendedTileMetricsOut.bin\n"
            "  CorrectedIntMetricsOut.bin\n",
            prog);
}

/* ============================================================
 * HTML helpers
 * ============================================================ */

/** Write a string escaping HTML special characters. */
static void html_escape(FILE *out, const char *s)
{
    for (; *s; s++) {
        switch (*s) {
        case '&':  fputs("&amp;",  out); break;
        case '<':  fputs("&lt;",   out); break;
        case '>':  fputs("&gt;",   out); break;
        case '"':  fputs("&quot;", out); break;
        case '\'': fputs("&#39;",  out); break;
        default:   fputc(*s, out);       break;
        }
    }
}

/* ============================================================
 * Per-lane aggregation helpers
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
 * CSS / HTML page template (emitted to file)
 * ============================================================ */

static void emit_html_head(FILE *out, const char *run_folder, const char *ts)
{
    fputs("<!DOCTYPE html>\n"
          "<html lang=\"en\">\n"
          "<head>\n"
          "<meta charset=\"UTF-8\">\n"
          "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
          "<title>InterOp Run Report</title>\n"
          "<style>\n"
          ":root{\n"
          "  --bg:#f0f2f5;--surface:#fff;--primary:#2563eb;--primary-dark:#1d4ed8;\n"
          "  --success:#16a34a;--warn:#d97706;--danger:#dc2626;--text:#1e293b;\n"
          "  --muted:#64748b;--border:#e2e8f0;--radius:8px;--shadow:0 1px 4px rgba(0,0,0,.1);\n"
          "}\n"
          "*{box-sizing:border-box;margin:0;padding:0}\n"
          "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;\n"
          "  background:var(--bg);color:var(--text);font-size:14px;line-height:1.5}\n"
          "a{color:var(--primary);text-decoration:none}\n"
          "a:hover{text-decoration:underline}\n"
          /* ---- Top header ---- */
          ".site-header{background:var(--primary);color:#fff;padding:16px 32px;\n"
          "  display:flex;align-items:center;gap:12px;box-shadow:0 2px 8px rgba(0,0,0,.2)}\n"
          ".site-header h1{font-size:1.4rem;font-weight:700}\n"
          ".site-header .meta{font-size:.8rem;opacity:.8;margin-left:auto;text-align:right}\n"
          /* ---- Layout ---- */
          ".page-wrap{display:flex;min-height:calc(100vh - 64px)}\n"
          ".sidebar{width:200px;flex-shrink:0;background:var(--surface);border-right:1px solid var(--border);\n"
          "  position:sticky;top:0;height:100vh;overflow-y:auto;padding:16px 0}\n"
          ".sidebar ul{list-style:none}\n"
          ".sidebar li a{display:block;padding:8px 20px;color:var(--muted);border-left:3px solid transparent;\n"
          "  font-size:.85rem;transition:all .15s}\n"
          ".sidebar li a:hover,.sidebar li a.active{\n"
          "  color:var(--primary);border-left-color:var(--primary);background:#eff6ff;\n"
          "  text-decoration:none}\n"
          ".sidebar .nav-group{font-size:.7rem;font-weight:700;color:var(--muted);\n"
          "  text-transform:uppercase;letter-spacing:.05em;padding:12px 20px 4px}\n"
          ".content{flex:1;padding:28px 32px;min-width:0}\n"
          /* ---- Section ---- */
          "section{margin-bottom:36px}\n"
          "section h2{font-size:1.1rem;font-weight:700;color:var(--text);\n"
          "  margin-bottom:16px;padding-bottom:8px;border-bottom:2px solid var(--border);\n"
          "  display:flex;align-items:center;gap:8px}\n"
          "section h2 .badge{font-size:.65rem;background:var(--primary);color:#fff;\n"
          "  padding:2px 8px;border-radius:999px;font-weight:600}\n"
          /* ---- Summary cards ---- */
          ".card-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(170px,1fr));gap:16px}\n"
          ".card{background:var(--surface);border-radius:var(--radius);padding:18px 20px;\n"
          "  box-shadow:var(--shadow);border-top:4px solid var(--primary)}\n"
          ".card.good{border-top-color:var(--success)}\n"
          ".card.warn{border-top-color:var(--warn)}\n"
          ".card.bad {border-top-color:var(--danger)}\n"
          ".card .label{font-size:.75rem;color:var(--muted);text-transform:uppercase;\n"
          "  letter-spacing:.05em;margin-bottom:6px}\n"
          ".card .value{font-size:1.6rem;font-weight:700;color:var(--text);line-height:1.1}\n"
          ".card .sub{font-size:.75rem;color:var(--muted);margin-top:4px}\n"
          /* ---- Charts ---- */
          ".chart-wrap{background:var(--surface);border-radius:var(--radius);\n"
          "  box-shadow:var(--shadow);padding:20px;margin-bottom:20px}\n"
          ".chart-wrap h3{font-size:.9rem;font-weight:600;margin-bottom:14px;color:var(--muted)}\n"
          ".chart-row{display:grid;grid-template-columns:1fr 1fr;gap:20px}\n"
          "@media(max-width:900px){.chart-row{grid-template-columns:1fr}}\n"
          "canvas{max-width:100%!important}\n"
          /* ---- Tables ---- */
          ".table-wrap{background:var(--surface);border-radius:var(--radius);\n"
          "  box-shadow:var(--shadow);overflow:auto;margin-bottom:20px}\n"
          "table{width:100%;border-collapse:collapse;font-size:.83rem}\n"
          "thead th{background:#f8fafc;border-bottom:2px solid var(--border);\n"
          "  padding:10px 14px;text-align:left;font-weight:600;color:var(--muted);\n"
          "  white-space:nowrap}\n"
          "tbody tr:nth-child(even){background:#f8fafc}\n"
          "tbody tr:hover{background:#eff6ff}\n"
          "tbody td{padding:8px 14px;border-bottom:1px solid var(--border)}\n"
          "tfoot td{padding:8px 14px;font-weight:600;background:#f1f5f9;\n"
          "  border-top:2px solid var(--border)}\n"
          /* ---- Quality badges ---- */
          ".q-good{color:var(--success);font-weight:600}\n"
          ".q-warn{color:var(--warn);font-weight:600}\n"
          ".q-bad {color:var(--danger);font-weight:600}\n"
          /* ---- Progress bar ---- */
          ".pbar-wrap{background:#e2e8f0;border-radius:999px;height:8px;min-width:80px}\n"
          ".pbar{height:8px;border-radius:999px;background:var(--primary);transition:width .3s}\n"
          ".pbar.good{background:var(--success)}\n"
          ".pbar.warn{background:var(--warn)}\n"
          /* ---- Footer ---- */
          "footer{text-align:center;padding:20px;font-size:.75rem;color:var(--muted)}\n"
          "</style>\n"
          "</head>\n"
          "<body>\n"
          "<header class=\"site-header\">\n"
          "  <div>\n"
          "    <h1>&#x1F9EC; Illumina Sequencing Run Report</h1>\n"
          "  </div>\n"
          "  <div class=\"meta\">\n"
          "    <div>Run folder: <strong>", out);
    html_escape(out, run_folder);
    fputs("</strong></div>\n"
          "    <div>Generated: ", out);
    html_escape(out, ts);
    fputs("</div>\n"
          "  </div>\n"
          "</header>\n"
          "<div class=\"page-wrap\">\n"
          "<nav class=\"sidebar\">\n"
          "  <div class=\"nav-group\">Overview</div>\n"
          "  <ul>\n"
          "    <li><a href=\"#summary\">&#x1F4CA; Summary</a></li>\n"
          "  </ul>\n"
          "  <div class=\"nav-group\">Quality</div>\n"
          "  <ul>\n"
          "    <li><a href=\"#qscore\">Q-Score Distribution</a></li>\n"
          "    <li><a href=\"#qcycle\">Q-Score by Cycle</a></li>\n"
          "    <li><a href=\"#error\">Error Rate</a></li>\n"
          "  </ul>\n"
          "  <div class=\"nav-group\">Intensity</div>\n"
          "  <ul>\n"
          "    <li><a href=\"#intensity\">Intensity by Cycle</a></li>\n"
          "  </ul>\n"
          "  <div class=\"nav-group\">Clusters</div>\n"
          "  <ul>\n"
          "    <li><a href=\"#lanes\">Per-Lane Metrics</a></li>\n"
          "    <li><a href=\"#index\">Index Summary</a></li>\n"
          "  </ul>\n"
          "  <div class=\"nav-group\">Detail</div>\n"
          "  <ul>\n"
          "    <li><a href=\"#runmeta\">Run Metrics</a></li>\n"
          "  </ul>\n"
          "</nav>\n"
          "<main class=\"content\">\n", out);
}

/* ============================================================
 * Emit run summary section
 * ============================================================ */

static void emit_summary_section(FILE *out,
    uint64_t total_reads, uint64_t q30_reads,
    double pct_q30,
    double avg_error,
    double pf_clusters, double raw_clusters,
    double occupied_clusters,
    int has_srm)
{
    double pct_pf = (raw_clusters > 0) ? pf_clusters / raw_clusters * 100.0 : 0.0;

    /* Colour coding for Q30 */
    const char *q30_cls = (pct_q30 >= 80.0) ? "good" :
                          (pct_q30 >= 70.0) ? "warn" : "bad";
    const char *err_cls = (avg_error < 0.5)  ? "good" :
                          (avg_error < 1.5)  ? "warn" : "bad";

    fputs("<section id=\"summary\">\n"
          "<h2>&#x1F4CA; Run Summary</h2>\n"
          "<div class=\"card-grid\">\n", out);

    /* Total reads card */
    fprintf(out,
        "<div class=\"card\">\n"
        "  <div class=\"label\">Total Reads</div>\n"
        "  <div class=\"value\">%.2fM</div>\n"
        "  <div class=\"sub\">%llu reads</div>\n"
        "</div>\n",
        (double)total_reads / 1e6,
        (unsigned long long)total_reads);

    /* Q30 card */
    fprintf(out,
        "<div class=\"card %s\">\n"
        "  <div class=\"label\">%%&nbsp;&ge;&nbsp;Q30</div>\n"
        "  <div class=\"value\">%.1f%%</div>\n"
        "  <div class=\"sub\">%llu reads &ge; Q30</div>\n"
        "</div>\n",
        q30_cls, pct_q30,
        (unsigned long long)q30_reads);

    /* Error rate card */
    if (avg_error >= 0.0) {
        fprintf(out,
            "<div class=\"card %s\">\n"
            "  <div class=\"label\">Avg Error Rate</div>\n"
            "  <div class=\"value\">%.2f%%</div>\n"
            "  <div class=\"sub\">PhiX error rate</div>\n"
            "</div>\n",
            err_cls, avg_error);
    }

    /* PF clusters card */
    if (has_srm && pf_clusters > 0) {
        fprintf(out,
            "<div class=\"card good\">\n"
            "  <div class=\"label\">PF Clusters</div>\n"
            "  <div class=\"value\">%.2fM</div>\n"
            "  <div class=\"sub\">%%PF: %.1f%%</div>\n"
            "</div>\n",
            pf_clusters / 1e6, pct_pf);

        if (occupied_clusters > 0) {
            double pct_occ = (pf_clusters > 0)
                ? occupied_clusters / pf_clusters * 100.0 : 0.0;
            const char *occ_cls = (pct_occ >= 85.0) ? "good" :
                                  (pct_occ >= 70.0) ? "warn" : "bad";
            fprintf(out,
                "<div class=\"card %s\">\n"
                "  <div class=\"label\">Occupied</div>\n"
                "  <div class=\"value\">%.1f%%</div>\n"
                "  <div class=\"sub\">%.2fM occupied clusters</div>\n"
                "</div>\n",
                occ_cls, pct_occ, occupied_clusters / 1e6);
        }
    }

    fputs("</div>\n</section>\n", out);
}

/* ============================================================
 * Emit q-score histogram section (chart + table)
 * ============================================================ */

static void emit_qscore_section(FILE *out,
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

    fputs("<section id=\"qscore\">\n"
          "<h2>Q-Score Distribution</h2>\n"
          "<div class=\"chart-wrap\">\n"
          "  <h3>Q-Score Histogram (all cycles, all lanes)</h3>\n"
          "  <canvas id=\"qHistChart\" height=\"90\"></canvas>\n"
          "</div>\n"
          "<div class=\"table-wrap\">\n"
          "<table>\n"
          "<thead><tr>"
          "<th>Q-Score</th><th>Reads</th><th>%% of Total</th>"
          "<th>Distribution</th></tr></thead>\n"
          "<tbody>\n", out);

    for (j = 0; j < qm->num_bins; j++) {
        double pct = (total > 0) ? (double)histogram[j] / (double)total * 100.0 : 0.0;
        const char *cls = (qm->bin_value[j] >= 30) ? "q-good" :
                          (qm->bin_value[j] >= 20) ? "q-warn" : "q-bad";
        fprintf(out,
            "<tr>"
            "<td><span class=\"%s\">Q%u</span></td>"
            "<td>%llu</td>"
            "<td>%.2f%%</td>"
            "<td><div class=\"pbar-wrap\"><div class=\"pbar%s\" style=\"width:%.1f%%\"></div></div></td>"
            "</tr>\n",
            cls, qm->bin_value[j],
            (unsigned long long)histogram[j],
            pct,
            (qm->bin_value[j] >= 30) ? " good" : (qm->bin_value[j] >= 20) ? " warn" : "",
            pct > 100.0 ? 100.0 : pct);
    }
    fputs("</tbody></table>\n</div>\n</section>\n", out);

    /* Emit Chart.js data as a JS variable for later use */
    fputs("<script>\n"
          "var qHistLabels=[", out);
    for (j = 0; j < qm->num_bins; j++) {
        if (j) fputc(',', out);
        fprintf(out, "'Q%u'", qm->bin_value[j]);
    }
    fputs("];\nvar qHistData=[", out);
    for (j = 0; j < qm->num_bins; j++) {
        if (j) fputc(',', out);
        fprintf(out, "%llu", (unsigned long long)histogram[j]);
    }
    fputs("];\nvar qHistColors=[", out);
    for (j = 0; j < qm->num_bins; j++) {
        if (j) fputc(',', out);
        if (qm->bin_value[j] >= 30)
            fputs("'rgba(22,163,74,0.8)'",  out);
        else if (qm->bin_value[j] >= 20)
            fputs("'rgba(217,119,6,0.8)'",  out);
        else
            fputs("'rgba(220,38,38,0.8)'",  out);
    }
    fputs("];\n</script>\n", out);
}

/* ============================================================
 * Emit q-score-by-cycle heatmap section
 * ============================================================ */

static void emit_qcycle_section(FILE *out, const interop_qmetrics_t *qm)
{
    uint16_t max_cycle = 0;
    size_t i;
    for (i = 0; i < qm->count; i++)
        if (qm->records[i].cycle > max_cycle)
            max_cycle = qm->records[i].cycle;

    if (max_cycle == 0) return;

    /* Per-cycle mean Q-score (weighted average) */
    double *sum_q   = (double *)calloc(max_cycle + 1, sizeof(double));
    uint64_t *cnt_q = (uint64_t *)calloc(max_cycle + 1, sizeof(uint64_t));
    double *pct30   = (double *)calloc(max_cycle + 1, sizeof(double));
    uint64_t *q30c  = (uint64_t *)calloc(max_cycle + 1, sizeof(uint64_t));
    uint64_t *totc  = (uint64_t *)calloc(max_cycle + 1, sizeof(uint64_t));

    if (!sum_q || !cnt_q || !pct30 || !q30c || !totc) {
        free(sum_q); free(cnt_q); free(pct30); free(q30c); free(totc);
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
    uint16_t cy;
    for (cy = 1; cy <= max_cycle; cy++)
        pct30[cy] = (totc[cy] > 0) ? (double)q30c[cy] / (double)totc[cy] * 100.0 : 0.0;

    fputs("<section id=\"qcycle\">\n"
          "<h2>Q-Score by Cycle</h2>\n"
          "<div class=\"chart-row\">\n"
          "  <div class=\"chart-wrap\">\n"
          "    <h3>Mean Q-Score per Cycle</h3>\n"
          "    <canvas id=\"qCycleChart\" height=\"120\"></canvas>\n"
          "  </div>\n"
          "  <div class=\"chart-wrap\">\n"
          "    <h3>%% &ge; Q30 per Cycle</h3>\n"
          "    <canvas id=\"q30CycleChart\" height=\"120\"></canvas>\n"
          "  </div>\n"
          "</div>\n"
          "</section>\n", out);

    fputs("<script>\n"
          "var qCycleLabels=[", out);
    for (cy = 1; cy <= max_cycle; cy++) {
        if (cy > 1) fputc(',', out);
        fprintf(out, "%u", cy);
    }
    fputs("];\nvar qCycleMean=[", out);
    for (cy = 1; cy <= max_cycle; cy++) {
        if (cy > 1) fputc(',', out);
        double mean = (cnt_q[cy] > 0) ? sum_q[cy] / (double)cnt_q[cy] : 0.0;
        fprintf(out, "%.2f", mean);
    }
    fputs("];\nvar q30CycleData=[", out);
    for (cy = 1; cy <= max_cycle; cy++) {
        if (cy > 1) fputc(',', out);
        fprintf(out, "%.2f", pct30[cy]);
    }
    fputs("];\n</script>\n", out);

    free(sum_q); free(cnt_q); free(pct30); free(q30c); free(totc);
}

/* ============================================================
 * Emit error rate section
 * ============================================================ */

static void emit_error_section(FILE *out,
    const interop_error_metrics_t *em)
{
    size_t i;
    uint16_t max_cycle = 0;
    for (i = 0; i < em->count; i++)
        if (em->records[i].cycle > max_cycle)
            max_cycle = em->records[i].cycle;

    if (max_cycle == 0) return;

    double   *sum_err = (double *)calloc(max_cycle + 1, sizeof(double));
    size_t   *cnt_err = (size_t *)calloc(max_cycle + 1, sizeof(size_t));

    if (!sum_err || !cnt_err) {
        free(sum_err); free(cnt_err);
        return;
    }

    for (i = 0; i < em->count; i++) {
        const interop_error_record_t *r = &em->records[i];
        sum_err[r->cycle] += r->error_rate;
        cnt_err[r->cycle]++;
    }

    fputs("<section id=\"error\">\n"
          "<h2>Error Rate by Cycle</h2>\n"
          "<div class=\"chart-wrap\">\n"
          "  <h3>Mean PhiX Error Rate (%) per Cycle</h3>\n"
          "  <canvas id=\"errorCycleChart\" height=\"90\"></canvas>\n"
          "</div>\n"
          "</section>\n", out);

    fputs("<script>\n"
          "var errCycleLabels=[", out);
    uint16_t cy;
    for (cy = 1; cy <= max_cycle; cy++) {
        if (cy > 1) fputc(',', out);
        fprintf(out, "%u", cy);
    }
    fputs("];\nvar errCycleData=[", out);
    for (cy = 1; cy <= max_cycle; cy++) {
        if (cy > 1) fputc(',', out);
        double v = (cnt_err[cy] > 0) ? sum_err[cy] / (double)cnt_err[cy] : 0.0;
        fprintf(out, "%.4f", v);
    }
    fputs("];\n</script>\n", out);

    free(sum_err); free(cnt_err);
}

/* ============================================================
 * Emit intensity-by-cycle section
 * ============================================================ */

static void emit_intensity_section(FILE *out,
    const interop_extraction_metrics_t *xm)
{
    size_t i;
    uint16_t max_cycle = 0;
    for (i = 0; i < xm->count; i++)
        if (xm->records[i].cycle > max_cycle)
            max_cycle = xm->records[i].cycle;

    if (max_cycle == 0) return;

    double *sum_a = (double *)calloc(max_cycle + 1, sizeof(double));
    double *sum_c = (double *)calloc(max_cycle + 1, sizeof(double));
    double *sum_g = (double *)calloc(max_cycle + 1, sizeof(double));
    double *sum_t = (double *)calloc(max_cycle + 1, sizeof(double));
    size_t *cnt   = (size_t *)calloc(max_cycle + 1, sizeof(size_t));

    if (!sum_a || !sum_c || !sum_g || !sum_t || !cnt) {
        free(sum_a); free(sum_c); free(sum_g); free(sum_t); free(cnt);
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

    fputs("<section id=\"intensity\">\n"
          "<h2>Intensity by Cycle</h2>\n"
          "<div class=\"chart-wrap\">\n"
          "  <h3>Mean Channel Intensity per Cycle</h3>\n"
          "  <canvas id=\"intensityCycleChart\" height=\"90\"></canvas>\n"
          "</div>\n"
          "</section>\n", out);

    fputs("<script>\n"
          "var intCycleLabels=[", out);
    uint16_t cy;
    for (cy = 1; cy <= max_cycle; cy++) {
        if (cy > 1) fputc(',', out);
        fprintf(out, "%u", cy);
    }
    fputs("];\n", out);
    /* Channel A */
    fputs("var intCycleA=[", out);
    for (cy = 1; cy <= max_cycle; cy++) {
        if (cy > 1) fputc(',', out);
        fprintf(out, "%.1f", cnt[cy] ? sum_a[cy] / cnt[cy] : 0.0);
    }
    fputs("];\n", out);
    /* Channel C */
    fputs("var intCycleC=[", out);
    for (cy = 1; cy <= max_cycle; cy++) {
        if (cy > 1) fputc(',', out);
        fprintf(out, "%.1f", cnt[cy] ? sum_c[cy] / cnt[cy] : 0.0);
    }
    fputs("];\n", out);
    /* Channel G */
    fputs("var intCycleG=[", out);
    for (cy = 1; cy <= max_cycle; cy++) {
        if (cy > 1) fputc(',', out);
        fprintf(out, "%.1f", cnt[cy] ? sum_g[cy] / cnt[cy] : 0.0);
    }
    fputs("];\n", out);
    /* Channel T */
    fputs("var intCycleT=[", out);
    for (cy = 1; cy <= max_cycle; cy++) {
        if (cy > 1) fputc(',', out);
        fprintf(out, "%.1f", cnt[cy] ? sum_t[cy] / cnt[cy] : 0.0);
    }
    fputs("];\n</script>\n", out);

    free(sum_a); free(sum_c); free(sum_g); free(sum_t); free(cnt);
}

/* ============================================================
 * Emit per-lane metrics section
 * ============================================================ */

static void emit_lane_section(FILE *out,
    const lane_stat_t *stats, int nlanes)
{
    if (nlanes <= 0) return;

    fputs("<section id=\"lanes\">\n"
          "<h2>Per-Lane Metrics</h2>\n"
          "<div class=\"chart-row\">\n"
          "  <div class=\"chart-wrap\">\n"
          "    <h3>Cluster Count per Lane</h3>\n"
          "    <canvas id=\"laneClusterChart\" height=\"150\"></canvas>\n"
          "  </div>\n"
          "  <div class=\"chart-wrap\">\n"
          "    <h3>Cluster Density per Lane (k/mm&sup2;)</h3>\n"
          "    <canvas id=\"laneDensityChart\" height=\"150\"></canvas>\n"
          "  </div>\n"
          "</div>\n"
          "<div class=\"table-wrap\">\n"
          "<table>\n"
          "<thead><tr>"
          "<th>Lane</th>"
          "<th>Cluster Count</th>"
          "<th>PF Clusters</th>"
          "<th>%%PF</th>"
          "<th>Density (k/mm&sup2;)</th>"
          "<th>Density PF</th>"
          "<th>Error Rate (%%)</th>"
          "<th>Intensity C1 (A/C/G/T)</th>"
          "</tr></thead>\n"
          "<tbody>\n", out);

    int k;
    for (k = 0; k < nlanes; k++) {
        const lane_stat_t *s = &stats[k];
        double cc  = s->n_cluster    ? s->cluster_count    / (double)s->n_cluster    : 0.0;
        double ccpf= s->n_cluster_pf ? s->cluster_count_pf / (double)s->n_cluster_pf : 0.0;
        double den = s->n_density    ? s->density          / (double)s->n_density    : 0.0;
        double denpf= s->n_density_pf? s->density_pf       / (double)s->n_density_pf: 0.0;
        double pctpf = (cc > 0) ? ccpf / cc * 100.0 : 0.0;
        double err  = s->n_error     ? s->error_rate_sum   / (double)s->n_error      : -1.0;
        double ia = s->n_intensity_c1 ? s->intensity_c1[0] / (double)s->n_intensity_c1 : 0.0;
        double ic = s->n_intensity_c1 ? s->intensity_c1[1] / (double)s->n_intensity_c1 : 0.0;
        double ig = s->n_intensity_c1 ? s->intensity_c1[2] / (double)s->n_intensity_c1 : 0.0;
        double it = s->n_intensity_c1 ? s->intensity_c1[3] / (double)s->n_intensity_c1 : 0.0;

        const char *pf_cls = (pctpf >= 80) ? "q-good" : (pctpf >= 60) ? "q-warn" : "q-bad";
        const char *err_cls = (err >= 0 && err < 0.5) ? "q-good" :
                              (err >= 0 && err < 1.5) ? "q-warn" : "q-bad";

        fprintf(out,
            "<tr>"
            "<td><strong>%u</strong></td>"
            "<td>%.2fM</td>"
            "<td>%.2fM</td>"
            "<td><span class=\"%s\">%.1f%%</span></td>"
            "<td>%.1f</td>"
            "<td>%.1f</td>",
            s->lane,
            cc / 1e6, ccpf / 1e6,
            pf_cls, pctpf,
            den, denpf);

        if (err >= 0)
            fprintf(out, "<td><span class=\"%s\">%.3f%%</span></td>", err_cls, err);
        else
            fputs("<td>N/A</td>", out);

        fprintf(out,
            "<td>%.0f / %.0f / %.0f / %.0f</td>"
            "</tr>\n",
            ia, ic, ig, it);
    }

    fputs("</tbody></table>\n</div>\n</section>\n", out);

    /* Emit lane chart data */
    fputs("<script>\n"
          "var laneLabels=[", out);
    for (k = 0; k < nlanes; k++) {
        if (k) fputc(',', out);
        fprintf(out, "'Lane %u'", stats[k].lane);
    }
    fputs("];\nvar laneCC=[", out);
    for (k = 0; k < nlanes; k++) {
        if (k) fputc(',', out);
        double cc = stats[k].n_cluster ? stats[k].cluster_count / (double)stats[k].n_cluster : 0.0;
        fprintf(out, "%.2f", cc / 1e6);
    }
    fputs("];\nvar laneCCPF=[", out);
    for (k = 0; k < nlanes; k++) {
        if (k) fputc(',', out);
        double v = stats[k].n_cluster_pf ? stats[k].cluster_count_pf / (double)stats[k].n_cluster_pf : 0.0;
        fprintf(out, "%.2f", v / 1e6);
    }
    fputs("];\nvar laneDen=[", out);
    for (k = 0; k < nlanes; k++) {
        if (k) fputc(',', out);
        double v = stats[k].n_density ? stats[k].density / (double)stats[k].n_density : 0.0;
        fprintf(out, "%.1f", v);
    }
    fputs("];\nvar laneDenPF=[", out);
    for (k = 0; k < nlanes; k++) {
        if (k) fputc(',', out);
        double v = stats[k].n_density_pf ? stats[k].density_pf / (double)stats[k].n_density_pf : 0.0;
        fprintf(out, "%.1f", v);
    }
    fputs("];\n</script>\n", out);
}

/* ============================================================
 * Emit index summary section
 * ============================================================ */

typedef struct {
    char     sample [INTEROP_MAX_NAME_LEN];
    char     index  [INTEROP_MAX_NAME_LEN];
    char     project[INTEROP_MAX_NAME_LEN];
    uint16_t lane;
    uint64_t count;
} html_sample_stat_t;

static void emit_index_section(FILE *out,
    const interop_index_metrics_t *im)
{
    if (im->count == 0) return;

    html_sample_stat_t *stats = NULL;
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
                html_sample_stat_t *tmp =
                    (html_sample_stat_t *)realloc(stats, nc * sizeof(html_sample_stat_t));
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

    fputs("<section id=\"index\">\n"
          "<h2>Index Summary</h2>\n"
          "<div class=\"chart-wrap\">\n"
          "  <h3>Cluster Count per Sample</h3>\n"
          "  <canvas id=\"indexChart\" height=\"100\"></canvas>\n"
          "</div>\n"
          "<div class=\"table-wrap\">\n"
          "<table>\n"
          "<thead><tr>"
          "<th>Lane</th><th>Sample</th><th>Index</th><th>Project</th>"
          "<th>Clusters</th><th>%% of Total</th><th>Distribution</th>"
          "</tr></thead>\n"
          "<tbody>\n", out);

    for (i = 0; i < nstats; i++) {
        double frac = (total_count > 0)
            ? (double)stats[i].count / (double)total_count * 100.0 : 0.0;
        fprintf(out,
            "<tr><td>%u</td><td>", stats[i].lane);
        html_escape(out, stats[i].sample);
        fputs("</td><td><code>", out);
        html_escape(out, stats[i].index);
        fputs("</code></td><td>", out);
        html_escape(out, stats[i].project);
        fprintf(out,
            "</td><td>%llu</td><td>%.2f%%</td>"
            "<td><div class=\"pbar-wrap\"><div class=\"pbar good\" style=\"width:%.1f%%\"></div></div></td>"
            "</tr>\n",
            (unsigned long long)stats[i].count, frac,
            frac > 100.0 ? 100.0 : frac);
    }

    fprintf(out,
        "<tfoot><tr><td colspan=\"4\">Total</td>"
        "<td>%llu</td><td>100.00%%</td><td></td></tr></tfoot>\n",
        (unsigned long long)total_count);

    fputs("</tbody></table>\n</div>\n</section>\n", out);

    /* Emit chart data */
    fputs("<script>\n"
          "var indexLabels=[", out);
    for (i = 0; i < nstats; i++) {
        if (i) fputc(',', out);
        fputc('\'', out);
        /* Escape single quotes for JS */
        const char *s = stats[i].sample;
        for (; *s; s++) {
            if (*s == '\'') fputs("\\'", out);
            else fputc(*s, out);
        }
        fputc('\'', out);
    }
    fputs("];\nvar indexData=[", out);
    for (i = 0; i < nstats; i++) {
        if (i) fputc(',', out);
        fprintf(out, "%llu", (unsigned long long)stats[i].count);
    }
    fputs("];\n</script>\n", out);

    free(stats);
}

/* ============================================================
 * Emit run metadata section
 * ============================================================ */

static void emit_runmeta_section(FILE *out,
    const interop_summary_run_metrics_t *srm,
    const interop_extended_tile_metrics_t *etm,
    int has_srm, int has_etm)
{
    fputs("<section id=\"runmeta\">\n"
          "<h2>Run Metrics</h2>\n"
          "<div class=\"table-wrap\">\n"
          "<table>\n"
          "<thead><tr><th>Metric</th><th>Value</th></tr></thead>\n"
          "<tbody>\n", out);

    if (has_srm && srm->count > 0) {
        const interop_summary_run_record_t *r = &srm->records[0];
        double pct_pf = (r->raw_cluster_count > 0)
            ? r->pf_cluster_count / r->raw_cluster_count * 100.0 : 0.0;
        fprintf(out,
            "<tr><td>Raw Cluster Count</td><td>%.5e</td></tr>\n"
            "<tr><td>PF Cluster Count</td><td>%.5e</td></tr>\n"
            "<tr><td>Occupancy Cluster Count</td><td>%.5e</td></tr>\n"
            "<tr><td>Occupancy Proxy Cluster Count</td><td>%.5e</td></tr>\n"
            "<tr><td>%% Passing Filter</td><td>%.2f%%</td></tr>\n"
            "<tr><td>Estimated PF Tbases (302 bp avg)</td><td>%.4f Tbases</td></tr>\n",
            r->raw_cluster_count,
            r->pf_cluster_count,
            r->occupancy_cluster_count,
            r->occupancy_proxy_cluster_count,
            pct_pf,
            /* 302 bp is used as a representative average total read length
             * for Illumina runs (e.g. 2×151 bp paired-end), matching the
             * same estimate used by interop_print_run_summary(). */
            r->pf_cluster_count * 302.0 / 1e12);
    }

    if (has_etm && etm->count > 0) {
        double total_occ = 0.0;
        size_t i;
        for (i = 0; i < etm->count; i++)
            total_occ += etm->records[i].cluster_count_occupied;
        fprintf(out,
            "<tr><td>Extended Tile Records</td><td>%zu</td></tr>\n"
            "<tr><td>Total Occupied Clusters (all tiles)</td><td>%.5e</td></tr>\n",
            etm->count, total_occ);
    }

    fputs("</tbody></table>\n</div>\n</section>\n", out);
}

/* ============================================================
 * Emit Chart.js initialization scripts
 * ============================================================ */

static void emit_chart_scripts(FILE *out,
    int has_qm, int has_em, int has_xm, int has_tm_lanes,
    int has_idx)
{
    fputs("<script src=\"https://cdn.jsdelivr.net/npm/chart.js@4/dist/chart.umd.min.js\"></script>\n"
          "<script>\n"
          "const gridColor='rgba(0,0,0,0.06)';\n"
          "const gridOpts={color:gridColor};\n"
          "function makeLineDataset(label,data,color){\n"
          "  return{label,data,borderColor:color,backgroundColor:color+'33',\n"
          "    fill:true,tension:0.3,pointRadius:0,borderWidth:1.5};\n"
          "}\n", out);

    if (has_qm) {
        fputs(
          "new Chart(document.getElementById('qHistChart'),{\n"
          "  type:'bar',\n"
          "  data:{labels:qHistLabels,\n"
          "    datasets:[{label:'Reads',data:qHistData,\n"
          "      backgroundColor:qHistColors,borderWidth:0}]},\n"
          "  options:{responsive:true,plugins:{legend:{display:false}},\n"
          "    scales:{x:{title:{display:true,text:'Q-Score'},grid:gridOpts},\n"
          "            y:{title:{display:true,text:'Read Count'},grid:gridOpts}}}\n"
          "});\n"

          "new Chart(document.getElementById('qCycleChart'),{\n"
          "  type:'line',\n"
          "  data:{labels:qCycleLabels,\n"
          "    datasets:[makeLineDataset('Mean Q',qCycleMean,'#2563eb')]},\n"
          "  options:{responsive:true,plugins:{legend:{display:false}},\n"
          "    scales:{x:{title:{display:true,text:'Cycle'},grid:gridOpts},\n"
          "            y:{title:{display:true,text:'Mean Q-Score'},min:0,grid:gridOpts}}}\n"
          "});\n"

          "new Chart(document.getElementById('q30CycleChart'),{\n"
          "  type:'line',\n"
          "  data:{labels:qCycleLabels,\n"
          "    datasets:[makeLineDataset('%>=Q30',q30CycleData,'#16a34a')]},\n"
          "  options:{responsive:true,plugins:{legend:{display:false}},\n"
          "    scales:{x:{title:{display:true,text:'Cycle'},grid:gridOpts},\n"
          "            y:{title:{display:true,text:'%% >= Q30'},min:0,max:100,grid:gridOpts}}}\n"
          "});\n", out);
    }

    if (has_em) {
        fputs(
          "new Chart(document.getElementById('errorCycleChart'),{\n"
          "  type:'line',\n"
          "  data:{labels:errCycleLabels,\n"
          "    datasets:[makeLineDataset('Error Rate',errCycleData,'#dc2626')]},\n"
          "  options:{responsive:true,plugins:{legend:{display:false}},\n"
          "    scales:{x:{title:{display:true,text:'Cycle'},grid:gridOpts},\n"
          "            y:{title:{display:true,text:'Error Rate (%%)'},min:0,grid:gridOpts}}}\n"
          "});\n", out);
    }

    if (has_xm) {
        fputs(
          "new Chart(document.getElementById('intensityCycleChart'),{\n"
          "  type:'line',\n"
          "  data:{labels:intCycleLabels,\n"
          "    datasets:[\n"
          "      makeLineDataset('A',intCycleA,'#22c55e'),\n"
          "      makeLineDataset('C',intCycleC,'#3b82f6'),\n"
          "      makeLineDataset('G',intCycleG,'#f59e0b'),\n"
          "      makeLineDataset('T',intCycleT,'#ef4444')\n"
          "    ]},\n"
          "  options:{responsive:true,\n"
          "    scales:{x:{title:{display:true,text:'Cycle'},grid:gridOpts},\n"
          "            y:{title:{display:true,text:'Intensity'},min:0,grid:gridOpts}}}\n"
          "});\n", out);
    }

    if (has_tm_lanes) {
        fputs(
          "new Chart(document.getElementById('laneClusterChart'),{\n"
          "  type:'bar',\n"
          "  data:{labels:laneLabels,\n"
          "    datasets:[\n"
          "      {label:'Total (M)',data:laneCC,backgroundColor:'rgba(37,99,235,0.7)',borderWidth:0},\n"
          "      {label:'PF (M)',data:laneCCPF,backgroundColor:'rgba(22,163,74,0.7)',borderWidth:0}\n"
          "    ]},\n"
          "  options:{responsive:true,\n"
          "    scales:{x:{grid:gridOpts},y:{title:{display:true,text:'Clusters (M)'},grid:gridOpts}}}\n"
          "});\n"
          "new Chart(document.getElementById('laneDensityChart'),{\n"
          "  type:'bar',\n"
          "  data:{labels:laneLabels,\n"
          "    datasets:[\n"
          "      {label:'Density',data:laneDen,backgroundColor:'rgba(59,130,246,0.7)',borderWidth:0},\n"
          "      {label:'Density PF',data:laneDenPF,backgroundColor:'rgba(16,185,129,0.7)',borderWidth:0}\n"
          "    ]},\n"
          "  options:{responsive:true,\n"
          "    scales:{x:{grid:gridOpts},y:{title:{display:true,text:'k/mm²'},grid:gridOpts}}}\n"
          "});\n", out);
    }

    if (has_idx) {
        fputs(
          "new Chart(document.getElementById('indexChart'),{\n"
          "  type:'bar',\n"
          "  data:{labels:indexLabels,\n"
          "    datasets:[{label:'Clusters',data:indexData,\n"
          "      backgroundColor:'rgba(37,99,235,0.75)',borderWidth:0}]},\n"
          "  options:{indexAxis:'y',responsive:true,\n"
          "    plugins:{legend:{display:false}},\n"
          "    scales:{x:{title:{display:true,text:'Cluster Count'},grid:gridOpts},\n"
          "            y:{grid:gridOpts}}}\n"
          "});\n", out);
    }

    fputs("</script>\n", out);
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
        fprintf(stderr, "html_report: cannot open '%s' for writing\n", output_path);
        return 1;
    }

    /* Timestamp */
    char ts_buf[64];
    {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%d %H:%M:%S", tm_info);
    }

    /* ----------------------------------------------------------
     * Load all available InterOp files
     * ---------------------------------------------------------- */
    char path[4096];
    interop_qmetrics_t             qm  = {0};
    interop_tile_metrics_t         tm  = {0};
    interop_error_metrics_t        em  = {0};
    interop_extraction_metrics_t   xm  = {0};
    interop_index_metrics_t        im  = {0};
    interop_summary_run_metrics_t  srm = {0};
    interop_extended_tile_metrics_t etm = {0};
    interop_corrected_int_metrics_t cm  = {0};

    int has_qm  = 0, has_tm  = 0, has_em  = 0, has_xm  = 0;
    int has_im  = 0, has_srm = 0, has_etm = 0, has_cm  = 0;

    if (interop_build_filepath(run_folder, "QMetrics",          path, sizeof(path)))
        has_qm  = (interop_read_qmetrics(path, &qm)                   == 0);
    if (interop_build_filepath(run_folder, "TileMetrics",        path, sizeof(path)))
        has_tm  = (interop_read_tile_metrics(path, &tm)                == 0);
    if (interop_build_filepath(run_folder, "ErrorMetrics",       path, sizeof(path)))
        has_em  = (interop_read_error_metrics(path, &em)               == 0);
    if (interop_build_filepath(run_folder, "ExtractionMetrics",  path, sizeof(path)))
        has_xm  = (interop_read_extraction_metrics(path, &xm)          == 0);
    if (interop_build_filepath(run_folder, "IndexMetrics",       path, sizeof(path)))
        has_im  = (interop_read_index_metrics(path, &im)               == 0);
    if (interop_build_filepath(run_folder, "SummaryRunMetrics",  path, sizeof(path)))
        has_srm = (interop_read_summary_run(path, &srm)                == 0);
    if (interop_build_filepath(run_folder, "ExtendedTileMetrics",path, sizeof(path)))
        has_etm = (interop_read_extended_tile_metrics(path, &etm)      == 0);
    if (interop_build_filepath(run_folder, "CorrectedIntMetrics",path, sizeof(path)))
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

    double pf_clusters  = 0.0, raw_clusters = 0.0, occ_clusters = 0.0;
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

    /* ----------------------------------------------------------
     * Write HTML
     * ---------------------------------------------------------- */
    emit_html_head(out, run_folder, ts_buf);

    /* Summary section */
    emit_summary_section(out,
        total_reads, q30_reads, pct_q30,
        avg_error,
        pf_clusters, raw_clusters, occ_clusters,
        has_srm);

    /* Q-score sections */
    if (has_qm) {
        emit_qscore_section(out, &qm);
        emit_qcycle_section(out, &qm);
    } else {
        fputs("<section id=\"qscore\"><h2>Q-Score Distribution</h2>"
              "<p style=\"color:var(--muted)\">QMetricsOut.bin not found.</p></section>\n"
              "<section id=\"qcycle\"><h2>Q-Score by Cycle</h2>"
              "<p style=\"color:var(--muted)\">QMetricsOut.bin not found.</p></section>\n",
              out);
    }

    /* Error rate section */
    if (has_em) {
        emit_error_section(out, &em);
    } else {
        fputs("<section id=\"error\"><h2>Error Rate by Cycle</h2>"
              "<p style=\"color:var(--muted)\">ErrorMetricsOut.bin not found.</p></section>\n",
              out);
    }

    /* Intensity section */
    if (has_xm) {
        emit_intensity_section(out, &xm);
    } else {
        fputs("<section id=\"intensity\"><h2>Intensity by Cycle</h2>"
              "<p style=\"color:var(--muted)\">ExtractionMetricsOut.bin not found.</p></section>\n",
              out);
    }

    /* Per-lane metrics */
    if (nlanes > 0) {
        emit_lane_section(out, lane_stats, nlanes);
    } else {
        fputs("<section id=\"lanes\"><h2>Per-Lane Metrics</h2>"
              "<p style=\"color:var(--muted)\">TileMetricsOut.bin not found.</p></section>\n",
              out);
    }

    /* Index summary */
    if (has_im) {
        emit_index_section(out, &im);
    } else {
        fputs("<section id=\"index\"><h2>Index Summary</h2>"
              "<p style=\"color:var(--muted)\">IndexMetricsOut.bin not found.</p></section>\n",
              out);
    }

    /* Run metadata */
    emit_runmeta_section(out, &srm, &etm, has_srm, has_etm);

    /* Charts (Chart.js) */
    emit_chart_scripts(out,
        has_qm,
        has_em,
        has_xm,
        nlanes > 0,
        has_im && im.count > 0);

    /* Footer */
    fputs("</main>\n</div>\n"
          "<footer>\n"
          "  Generated by <strong>InterOpReader html_report</strong> &mdash; "
          "Charts powered by <a href=\"https://www.chartjs.org/\" target=\"_blank\">Chart.js</a>\n"
          "</footer>\n"
          "</body>\n</html>\n", out);

    /* Suppress unused variable warnings */
    (void)has_cm; (void)cm;

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

    fprintf(stderr, "html_report: report written to '%s'\n", output_path);
    return 0;
}
