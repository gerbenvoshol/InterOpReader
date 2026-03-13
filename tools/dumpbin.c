/*
 * dumpbin — Dump the raw binary content of an Illumina InterOp file
 *
 * Equivalent to the Illumina SAV "dumpbin" developer utility, which is used
 * to inspect the raw binary data in InterOp files.
 *
 * Usage:
 *   dumpbin <InterOp/FileOut.bin>
 *
 * Output:
 *   - File type, version, and record size from the header
 *   - Hex + ASCII dump of the entire file
 */

#define INTEROP_IMPLEMENTATION
#include "../interop_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s <InterOp/FileOut.bin>\n\n"
            "Dumps the binary content of an Illumina InterOp file as hex and\n"
            "ASCII.  Shows the detected file type and header metadata.\n",
            prog);
}

/** Print a hex + ASCII dump of buf[0..len-1], 16 bytes per row. */
static void hex_dump(const unsigned char *buf, size_t len, size_t offset)
{
    size_t i;
    for (i = 0; i < len; i += 16) {
        size_t row_end = i + 16 < len ? i + 16 : len;
        size_t j;

        printf("%08zx  ", offset + i);

        /* Hex bytes */
        for (j = i; j < i + 16; j++) {
            if (j < len)
                printf("%02x ", (unsigned int)buf[j]);
            else
                printf("   ");
            if (j == i + 7) printf(" ");
        }

        /* ASCII */
        printf(" |");
        for (j = i; j < row_end; j++)
            printf("%c", isprint(buf[j]) ? buf[j] : '.');
        printf("|\n");
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2) { usage(argv[0]); return 1; }

    const char *filename = argv[1];
    interop_file_type_t type = interop_detect_file_type(filename);

    /* Decode the type name */
    static const char * const type_names[] = {
        "Unknown",
        "QMetrics",
        "TileMetrics",
        "CorrectedIntMetrics",
        "IndexMetrics",
        "ErrorMetrics",
        "ExtractionMetrics",
        "SummaryRunMetrics",
        "ExtendedTileMetrics"
    };
    const char *type_name =
        (type >= 0 && type <= INTEROP_FILE_EXTENDED_TILE)
        ? type_names[type] : "Unknown";

    printf("File:      %s\n", filename);
    printf("Detected:  %s\n\n", type_name);

    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open '%s'\n", filename);
        return 1;
    }

    /* Read and display header fields common to most formats */
    unsigned char hdr[4] = {0};
    size_t n = fread(hdr, 1, sizeof(hdr), f);
    if (n >= 1) printf("Version:     %u\n",     (unsigned)hdr[0]);
    if (n >= 2) printf("Record Size: %u bytes\n\n", (unsigned)hdr[1]);

    /* Seek back to the beginning for the full dump */
    rewind(f);

    /* Read entire file into memory */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);

    if (file_size <= 0) {
        printf("(empty file)\n");
        fclose(f);
        return 0;
    }

    unsigned char *buf = (unsigned char *)malloc((size_t)file_size);
    if (!buf) {
        fprintf(stderr, "Error: out of memory\n");
        fclose(f);
        return 1;
    }

    size_t read_n = fread(buf, 1, (size_t)file_size, f);
    fclose(f);

    printf("File size: %ld bytes\n\n", file_size);
    hex_dump(buf, read_n, 0);

    free(buf);
    return 0;
}
