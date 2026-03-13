# Makefile for InterOpReader
#
# Builds the main dispatcher (interop_reader) and all tool executables.
#
# Usage:
#   make              Build everything
#   make all          Build everything
#   make clean        Remove built executables
#   make tools        Build only the tools in tools/
#
# Each binary links only against libc (and libm on some platforms).

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -O2 -I.
LDFLAGS = -lm

# Main dispatcher binary
MAIN    = interop_reader

# Tool names (built from tools/<name>.c)
TOOL_NAMES = \
    dumptext \
    dumpbin \
    summary \
    imaging_table \
    index_summary \
    plot_qscore_histogram \
    plot_qscore_heatmap \
    plot_by_cycle \
    plot_by_lane \
    plot_flowcell \
    plot_sample_qc \
    aggregate

TOOL_BINS = $(addprefix tools/, $(TOOL_NAMES))

.PHONY: all tools clean

all: $(MAIN) $(TOOL_BINS)

tools: $(TOOL_BINS)

# Main dispatcher
$(MAIN): main.c interop_reader.h
	$(CC) $(CFLAGS) -o $@ main.c $(LDFLAGS)

# Tool binaries — each is a self-contained translation unit that
# defines INTEROP_IMPLEMENTATION before including the header.
tools/%: tools/%.c interop_reader.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(MAIN) $(TOOL_BINS)
