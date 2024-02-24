#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#pragma pack(push, 1) // Ensure no padding is added by the compiler
// Define structures to represent header information
typedef struct {
    uint8_t version_number;
    uint8_t record_size;
    uint8_t has_bins;
} Header;

typedef struct {
    uint8_t bin_count;
    uint8_t* low_ends;
    uint8_t* high_ends;
    uint8_t* values;
} ExtendedHeader;

typedef struct {
    uint16_t lane_number;
    uint32_t tile_number;
    uint16_t cycle_number;
} BaseCycleMetric;

typedef struct {
    uint32_t q20_count;
    uint32_t q30_count;
    uint32_t total_count;
    uint32_t median_score;
} QCollapsedMetric;
#pragma pack(pop)

// Function to read the QMetrics metric file
void readQMetricsFile(const char *filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        printf("Error opening file: %s\n", filename);
        return;
    }

    // Read header
    Header header;
    fread(&header, sizeof(Header), 1, file);
    printf("Version Number: %u\n", header.version_number);
    printf("Record size: %u\n", header.record_size);
    printf("Has Bins: %s\n", header.has_bins ? "Yes" : "No");

    // Read extended header if present
    ExtendedHeader extended_header;
    if (header.has_bins) {
        fread(&extended_header.bin_count, sizeof(uint8_t), 1, file);
        extended_header.low_ends = calloc(sizeof(uint8_t), extended_header.bin_count);
        extended_header.high_ends = calloc(sizeof(uint8_t), extended_header.bin_count);
        extended_header.values = calloc(sizeof(uint8_t), extended_header.bin_count);

        for (int i = 0; i < extended_header.bin_count; i++) {
            fread(extended_header.low_ends+i, sizeof(uint8_t), 1, file);
            fread(extended_header.high_ends+i, sizeof(uint8_t), 1, file);
            fread(extended_header.values+i, sizeof(uint8_t), 1, file);
        }

        printf("Number of bins: %u\n", extended_header.bin_count);
        for (int i = 0; i < extended_header.bin_count; i++) {
            printf("Bin %i (low, high, value): ", i+1);
            printf("%u,", extended_header.low_ends[i]);
            printf(" %u,", extended_header.high_ends[i]);
            printf(" %u\n", extended_header.values[i]);
        }
        
    } else {
        extended_header.bin_count = 50;
    }


    // Read n-records
    BaseCycleMetric base_cycle_metric;
    uint32_t* histogram = calloc(sizeof(uint32_t), extended_header.bin_count);
    while (!feof(file)) {
        fread(&base_cycle_metric.lane_number, sizeof(uint16_t), 1, file);
        fread(&base_cycle_metric.tile_number, sizeof(uint32_t), 1, file);
        fread(&base_cycle_metric.cycle_number, sizeof(uint16_t), 1, file);

        printf("Lane: %u, Tile: %u, Cycle: %u\n", base_cycle_metric.lane_number, base_cycle_metric.tile_number, base_cycle_metric.cycle_number);

        fread(histogram, sizeof(uint32_t), extended_header.bin_count, file);
        printf("Histogram: ");
        for (int i = 0; i < extended_header.bin_count; i++) {
            printf("%u", histogram[i]);
            if (i < extended_header.bin_count-1)
                printf(", ");
        }
        printf("\n");

        // printf("Q20 Count: %u, Q30 Count: %u, Total Count: %u, Median Score: %u\n", 
        //     q_collapsed_metric.q20_count, q_collapsed_metric.q30_count, q_collapsed_metric.total_count, q_collapsed_metric.median_score);
    }

    // Clean up
    if (header.has_bins) {
        free(extended_header.low_ends);
        free(extended_header.high_ends);
        free(extended_header.values);
    }

    fclose(file);
}

#pragma pack(push, 1) // Ensure no padding is added by the compiler
// Define the structure to hold the metric data
struct SummaryRunRecord {
    int16_t dummy;
    int32_t size; // Added undocumented field size
    double occupancy_proxy_cluster_count;
    double raw_cluster_count;
    double occupancy_cluster_count;
    double PF_cluster_count;
};
#pragma pack(pop)

// Helper function to safely perform division and avoid division by zero
double safeDivide(double numerator, double denominator) {
    return (denominator == 0) ? 0 : numerator / denominator;
}

// Function to read the SummaryRun metric file
void readSummaryRunFile(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        printf("Error opening file: %s\n", filename);
        return;
    }

    // Read the version number
    uint8_t version;
    if (fread(&version, sizeof(uint8_t), 1, file) != 1) {
        printf("Failed to read version number.\n");
        fclose(file);
        return;
    }
    printf("Version Number: %u\n", version);

    // Read the records
    struct SummaryRunRecord record;
    while (fread(&record, sizeof(struct SummaryRunRecord), 1, file) == 1) {
        // Process each record
        // printf("Dummy: %u\n", record.dummy);
        // printf("Size: %u\n", record.size);
        printf("Occupancy Proxy Cluster Count: %.5e\n", record.occupancy_proxy_cluster_count);
        printf("Raw Cluster Count: %.5e\n", record.raw_cluster_count);
        printf("Occupancy Cluster Count: %.5e\n", record.occupancy_cluster_count);
        printf("PF Cluster Count: %.5e\n", record.PF_cluster_count);

        // Calculate and print percentages
        double percent_occupancy_proxy = safeDivide(record.occupancy_proxy_cluster_count, record.PF_cluster_count) * 100;
        double percent_pf = safeDivide(record.PF_cluster_count, record.raw_cluster_count) * 100;
        double percent_occupied = safeDivide(record.occupancy_cluster_count, record.raw_cluster_count) * 100;

        printf("Percent Occupancy Proxy: %.2f%%\n", percent_occupancy_proxy);
        printf("Percent PF: %.2f%%\n", percent_pf);
        printf("Percent Occupied: %.2f%%\n", percent_occupied);
    }

    if (!feof(file)) {
        perror("Failed to read file to the end");
    }

    fclose(file);
}

typedef struct {
    uint8_t version;
    uint8_t record_size;
    float density;
} FileHeader;

typedef struct {
    uint16_t lane_number;
    uint32_t tile_number;
    char code;
} RecordBase;

int readTileMetricsFile(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror("Error opening file");
        return 1;
    }

    // Read file header
    FileHeader header;
    fread(&header.version, sizeof(uint8_t), 1, file);
    fread(&header.record_size, sizeof(uint8_t), 1, file);
    fread(&header.density, sizeof(float), 1, file);

    printf("File Version: %d\n", header.version);
    printf("Record Size: %d\n", header.record_size);
    printf("Density: %f\n", header.density);

    // Read records
    while (!feof(file)) {
        RecordBase base;
        fread(&base.lane_number, sizeof(uint16_t), 1, file);
        fread(&base.tile_number, sizeof(uint32_t), 1, file);
        fread(&base.code, sizeof(char), 1, file);

        printf("\nLane Number: %d, Tile Number: %u, Code: %c\n", base.lane_number, base.tile_number, base.code);

        if (base.code == 't') {
            float cluster_count, pf_cluster_count;
            fread(&cluster_count, sizeof(float), 1, file);
            fread(&pf_cluster_count, sizeof(float), 1, file);
            printf("Cluster Count: %f, PF Cluster Count: %f\n", cluster_count, pf_cluster_count);
        } else if (base.code == 'r') {
            uint32_t read_number;
            float percent_aligned;
            fread(&read_number, sizeof(uint32_t), 1, file);
            fread(&percent_aligned, sizeof(float), 1, file);
            printf("Read Number: %u, Percent Aligned: %f\n", read_number, percent_aligned);
        }
    }

    fclose(file);
    return 0;
}

#pragma pack(push, 1)
typedef struct {
    uint16_t lane_number;
    uint32_t tile_number;
    uint16_t read_number;
} BaseReadMetric;

typedef struct {
    uint16_t index_name_length;
    // Note: index_name is dynamic
    uint64_t index_cluster_count;
    uint16_t sample_name_length;
    // Note: sample_name is dynamic
    uint16_t project_name_length;
    // Note: project_name is dynamic
} IndexMetricHeader;
#pragma pack(pop)

void readStringFromFile(FILE* file, uint16_t length) {
    char* buffer = (char*)malloc(length + 1);
    if (buffer) {
        fread(buffer, sizeof(char), length, file);
        buffer[length] = '\0'; // Null-terminate the string
        printf("%s\n", buffer);
        free(buffer);
    } else {
        // Handle allocation failure
        printf("Failed to allocate memory for string\n");
    }
}

void parseIndexMetricsFile(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        printf("Error opening file: %s\n", filename);
        return;
    }

    uint8_t version;
    if (fread(&version, sizeof(version), 1, file) != 1) {
        printf("Failed to read version number.\n");
        fclose(file);
        return;
    }
    printf("Version Number: %u\n", version);

    BaseReadMetric baseMetric;
    IndexMetricHeader indexMetricHeader;

    while (fread(&baseMetric, sizeof(BaseReadMetric), 1, file) == 1) {
        printf("Lane: %u, Tile: %u, Read: %u\n", baseMetric.lane_number, baseMetric.tile_number, baseMetric.read_number);

        if (fread(&indexMetricHeader, sizeof(uint16_t) + sizeof(uint64_t), 1, file) != 1) {
            break; // Failed to read the next part of the header, potentially end of file
        }
        
        printf("Index Name Length: %u\n", indexMetricHeader.index_name_length);
        readStringFromFile(file, indexMetricHeader.index_name_length);
        
        if (fread(&indexMetricHeader.index_cluster_count, sizeof(uint64_t), 1, file) != 1) {
            break;
        }

        printf("Index Cluster Count: %lu\n", indexMetricHeader.index_cluster_count);

        if (fread(&indexMetricHeader.sample_name_length, sizeof(uint16_t), 1, file) != 1) {
            break;
        }

        printf("Sample Name Length: %u\n", indexMetricHeader.sample_name_length);
        readStringFromFile(file, indexMetricHeader.sample_name_length);

        if (fread(&indexMetricHeader.project_name_length, sizeof(uint16_t), 1, file) != 1) {
            break;
        }

        printf("Project Name Length: %u\n", indexMetricHeader.project_name_length);
        readStringFromFile(file, indexMetricHeader.project_name_length);
    }

    fclose(file);
}

#pragma pack(push, 1) // Ensure no padding is added by the compiler
typedef struct {
    uint32_t noCall;
    uint32_t baseA;
    uint32_t baseC;
    uint32_t baseG;
    uint32_t baseT;
} CorrectedIntensityMetric;
#pragma pack(pop)

void parseCorrectedIntensityMetricsFile(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        printf("Error opening file: %s\n", filename);
        return;
    }

    uint8_t version;
    uint8_t recordSize;
    if (fread(&version, sizeof(uint8_t), 1, file) != 1 || fread(&recordSize, sizeof(uint8_t), 1, file) != 1) {
        printf("Failed to read header.\n");
        fclose(file);
        return;
    }
    printf("Version Number: %u\n", version);
    printf("Record Size: %u\n", recordSize);

    if (recordSize != sizeof(BaseCycleMetric) + sizeof(CorrectedIntensityMetric)) {
        printf("Unexpected record size. Expected: %zu, Found: %u\n", sizeof(BaseCycleMetric) + sizeof(CorrectedIntensityMetric), recordSize);
        fclose(file);
        return;
    }

    BaseCycleMetric base_cycle_metric;
    CorrectedIntensityMetric intensityMetric;

    while (fread(&baseMetric, sizeof(BaseCycleMetric), 1, file) == 1) {
        if (fread(&intensityMetric, sizeof(CorrectedIntensityMetric), 1, file) != 1) {
            printf("Failed to read corrected intensity metric.\n");
            break; // Break the loop if we fail to read intensity metrics
        }
        printf("Lane: %u, Tile: %u, Cycle: %u\n", base_cycle_metric.lane_number, base_cycle_metric.tile_number, base_cycle_metric.cycle_number);
        printf("No Call: %u, A: %u, C: %u, G: %u, T: %u\n", 
               intensityMetric.noCall, 
               intensityMetric.baseA, 
               intensityMetric.baseC, 
               intensityMetric.baseG, 
               intensityMetric.baseT);
    }

    fclose(file);
}

int main(int argc, char *argv[]) {

    if (argc != 2) {
        fprintf(stderr, "Usage: %s InterOp/QMetrics.bin\n", argv[0]);
        return 1;
    }

    if(strstr(argv[1], "SummaryRunMetricsOut.bin")) {
        readSummaryRunFile(argv[1]);
    } else if (strstr(argv[1], "QMetricsOut.bin")) {
        readQMetricsFile(argv[1]);
    } else if (strstr(argv[1], "TileMetricsOut.bin")) {
        readTileMetricsFile(argv[1]);
    } else if (strstr(argv[1], "IndexMetricsOut.bin")) {
        parseIndexMetricsFile(argv[1]);
    } else if (strstr(argv[1], "CorrectedIntMetricsOut.bin")) {
        parseCorrectedIntensityMetricsFile(argv[1]);
    } else {
        fprintf(stderr, "Unsupported file: %s\n", argv[1]);
        return 1;
    }

    return 0;
}