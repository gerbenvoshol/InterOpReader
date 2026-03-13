# Illumina InterOp File Reader

A **single-header C library** (STB-style) for parsing Illumina InterOp binary
metric files, together with a suite of command-line tools equivalent to the
applications provided by the [Illumina interop C++ library](https://github.com/Illumina/interop).

> **Note:** This is not production-ready software.  Always verify results
> against official Illumina tooling.

---

## Library — `interop_reader.h`

The entire implementation lives in a single header file (`interop_reader.h`)
following the [STB single-file library](https://github.com/nothings/stb)
convention.

### Integration

In **exactly one** `.c` / `.cpp` translation unit, define
`INTEROP_IMPLEMENTATION` before including the header to pull in the
implementation:

```c
#define INTEROP_IMPLEMENTATION
#include "interop_reader.h"
```

In all other files just include the header normally:

```c
#include "interop_reader.h"
```

### Supported InterOp file formats

| File | Description | Versions |
|------|-------------|---------|
| `QMetricsOut.bin` | Q-score histograms per lane/tile/cycle | 4–7 |
| `TileMetricsOut.bin` | Cluster density and alignment per tile | 2 |
| `CorrectedIntMetricsOut.bin` | Base-call intensity counts | 2–4 |
| `IndexMetricsOut.bin` | Barcode index cluster assignment | 1–2 |
| `ErrorMetricsOut.bin` | PhiX error rate per lane/tile/cycle | 3–6 |
| `ExtractionMetricsOut.bin` | Channel intensities and FWHM | 2–3 |
| `SummaryRunMetricsOut.bin` | Run-level cluster count summary | 1 |
| `ExtendedTileMetricsOut.bin` | Extended tile occupancy metrics | 1–3 |

### Quick example

```c
#define INTEROP_IMPLEMENTATION
#include "interop_reader.h"
#include <stdio.h>

int main(void) {
    interop_qmetrics_t qm = {0};
    if (interop_read_qmetrics("InterOp/QMetricsOut.bin", &qm) == 0) {
        printf("%% >= Q30: %.2f%%\n", interop_compute_percent_q30(&qm));
        interop_free_qmetrics(&qm);
    }
    return 0;
}
```

---

## Building

Requires GCC (or any C99-compatible compiler) and GNU Make:

```bash
make          # build interop_reader + all tools
make tools    # build only the tools
make clean    # remove all built binaries
```

### Manual compilation

```bash
# Dispatcher / dumptext
gcc -O2 -o interop_reader main.c -lm

# Any tool (each is self-contained)
gcc -O2 -o tools/summary tools/summary.c -lm
```

---

## Tools

Each tool is a standalone C program that uses `interop_reader.h`.

### `interop_reader` — file dispatcher

Reads a single InterOp file and prints its contents in human-readable form.
File type is detected automatically from the filename.

```bash
./interop_reader InterOp/QMetricsOut.bin
./interop_reader InterOp/ErrorMetricsOut.bin
```

### `dumptext` — human-readable dump (SAV dumptext equivalent)

Same as `interop_reader`; prints one file as plain text.

```bash
./tools/dumptext InterOp/QMetricsOut.bin
```

### `dumpbin` — hex dump (developer tool)

Prints the file header metadata and a full hex + ASCII dump.
Useful for inspecting raw binary data.

```bash
./tools/dumpbin InterOp/TileMetricsOut.bin
```

### `summary` — run summary (SAV Summary Tab equivalent)

Reads QMetrics, TileMetrics, ErrorMetrics, ExtractionMetrics, and
SummaryRunMetrics from a run folder and prints a summary table.

```bash
./tools/summary /path/to/run_folder
```

### `imaging_table` — imaging table (SAV Imaging Tab equivalent)

Outputs a per-tile, per-cycle CSV table joining ExtractionMetrics,
ErrorMetrics, and CorrectedIntMetrics.

```bash
./tools/imaging_table /path/to/run_folder > imaging.csv
```

### `index_summary` — index summary (SAV Indexing Tab equivalent)

Outputs a per-sample barcode cluster count table as CSV.

```bash
./tools/index_summary /path/to/run_folder > index_summary.csv
```

### `aggregate` — per-cycle aggregation

Outputs a CSV of per-cycle aggregate statistics (total reads, Q30 reads,
mean intensity per channel).

```bash
./tools/aggregate /path/to/run_folder > per_cycle.csv
```

### `html_report` — self-contained HTML run report

Reads all available InterOp metric files from a run folder and generates a
self-contained HTML page with interactive charts, summary cards, per-cycle
plots, per-lane tables, and sample index summaries.

```bash
./tools/html_report -i /path/to/run_folder -o report.html
```

The report includes:

- **Summary cards** — total reads, % ≥ Q30, average error rate, PF cluster
  count, and flowcell occupancy (colour-coded green/amber/red).
- **Q-Score Distribution** — bar chart histogram plus a table with per-bin
  read counts and inline progress bars.
- **Q-Score by Cycle** — mean Q-score per cycle and % ≥ Q30 per cycle as
  line charts.
- **Error Rate by Cycle** — mean PhiX error rate per cycle line chart.
- **Intensity by Cycle** — mean channel intensity (A/C/G/T) per cycle line
  chart.
- **Per-Lane Metrics** — cluster count / density / % PF / error rate /
  cycle-1 intensity table plus bar charts.
- **Index Summary** — per-sample cluster counts, fraction mapped table, and
  horizontal bar chart (when `IndexMetricsOut.bin` is present).
- **Run Metrics** — full run-level cluster counts from `SummaryRunMetrics`
  and extended tile occupancy.

Charts are rendered with [Chart.js](https://www.chartjs.org/) (loaded from
jsDelivr CDN); an internet connection is required to view the charts.

### GNUPlot tools (SAV Analysis Tab equivalents)

All plot tools write a GNUPlot script to stdout.
Pipe directly to `gnuplot` to display, or redirect to a file.

| Tool | Description |
|------|-------------|
| `plot_qscore_histogram` | Q-score histogram (all cycles aggregated) |
| `plot_qscore_heatmap` | Q-score vs. cycle heatmap |
| `plot_by_cycle` | Mean channel intensity by cycle |
| `plot_by_lane` | Cluster count (total and PF) by lane |
| `plot_flowcell` | Flowcell intensity heatmap (channel A, cycle 1) |
| `plot_sample_qc` | Sample index cluster count bar chart |

```bash
./tools/plot_qscore_histogram /path/to/run_folder | gnuplot
./tools/plot_qscore_heatmap   /path/to/run_folder | gnuplot
./tools/plot_by_cycle         /path/to/run_folder | gnuplot
./tools/plot_by_lane          /path/to/run_folder | gnuplot
./tools/plot_flowcell         /path/to/run_folder | gnuplot
./tools/plot_sample_qc        /path/to/run_folder | gnuplot
```

---

## Run-folder layout

All tools that accept a `<run_folder>` path look for InterOp files in:

1. `<run_folder>/InterOp/<metric>Out.bin`  ← standard layout
2. `<run_folder>/<metric>Out.bin`           ← fallback (InterOp directory passed directly)

---

## Library API summary

```c
/* Detection */
interop_file_type_t interop_detect_file_type(const char *filename);
int                 interop_build_filepath(const char *run_folder,
                                           const char *metric_name,
                                           char *out, size_t out_size);

/* Parsers — each returns 0 on success */
int interop_read_qmetrics             (const char *, interop_qmetrics_t *);
int interop_read_tile_metrics         (const char *, interop_tile_metrics_t *);
int interop_read_corrected_int_metrics(const char *, interop_corrected_int_metrics_t *);
int interop_read_index_metrics        (const char *, interop_index_metrics_t *);
int interop_read_error_metrics        (const char *, interop_error_metrics_t *);
int interop_read_extraction_metrics   (const char *, interop_extraction_metrics_t *);
int interop_read_summary_run          (const char *, interop_summary_run_metrics_t *);
int interop_read_extended_tile_metrics(const char *, interop_extended_tile_metrics_t *);

/* Memory cleanup */
void interop_free_qmetrics            (interop_qmetrics_t *);
void interop_free_tile_metrics        (interop_tile_metrics_t *);
/* … (one free_* function per metric type) … */

/* Aggregate statistics */
double   interop_compute_percent_q30       (const interop_qmetrics_t *);
uint64_t interop_compute_total_reads       (const interop_qmetrics_t *);
uint64_t interop_compute_q30_reads         (const interop_qmetrics_t *);
double   interop_compute_avg_error_rate    (const interop_error_metrics_t *, int lane);
double   interop_compute_intensity_cycle1  (const interop_extraction_metrics_t *,
                                            int channel, int lane);

/* Print (human-readable) and CSV dump functions for each metric type */
void interop_print_qmetrics    (const interop_qmetrics_t *);
void interop_dump_qmetrics_csv (const interop_qmetrics_t *, FILE *);
/* … (equivalent functions for every metric type) … */

/* High-level run-folder tools */
void interop_print_run_summary           (const char *run_folder, FILE *);
void interop_print_imaging_table_csv     (const char *run_folder, FILE *);
void interop_print_index_summary_csv     (const char *run_folder, FILE *);
void interop_write_qscore_histogram_gnuplot(const char *run_folder, FILE *);
void interop_write_qscore_heatmap_gnuplot  (const char *run_folder, FILE *);
void interop_write_plot_by_cycle_gnuplot   (const char *run_folder, FILE *);
void interop_write_plot_by_lane_gnuplot    (const char *run_folder, FILE *);
void interop_write_flowcell_heatmap_gnuplot(const char *run_folder, FILE *);
void interop_write_sample_qc_gnuplot       (const char *run_folder, FILE *);
```

---

## License

This software is released into the public domain under the [Unlicense](LICENSE).

## Disclaimer

This software is provided "AS IS", without warranty of any kind.
Always verify results with official Illumina software or consult a qualified
professional when interpreting sequencing data.
