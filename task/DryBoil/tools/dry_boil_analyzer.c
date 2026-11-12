#include "dry_boil_detector.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_sample(char *line, float *temperature, uint8_t *power)
{
    char *field;
    unsigned int column = 0u;
    float parsed_temperature = 0.0f;
    long parsed_power = 0;

    field = strtok(line, ",");
    while (field != NULL) {
        if (column == 3u) parsed_temperature = strtof(field, NULL);
        if (column == 4u) {
            parsed_power = strtol(field, NULL, 10);
            if (parsed_power < 0) parsed_power = 0;
            if (parsed_power > 13) parsed_power = 13;
            *temperature = parsed_temperature;
            *power = (uint8_t)parsed_power;
            return 1;
        }
        column++;
        field = strtok(NULL, ",");
    }
    return 0;
}

static const char *state_name(db_state_t state)
{
    switch (state) {
    case DB_LEARNING: return "LEARNING";
    case DB_WATER_LIKELY: return "WATER_LIKELY";
    case DB_DRY_SUSPECTED: return "DRY_SUSPECTED";
    case DB_DRY_CONFIRMED: return "DRY_CONFIRMED";
    default: return "UNKNOWN";
    }
}

static int replay(const char *path)
{
    FILE *file = fopen(path, "r");
    char line[2048];
    unsigned long row = 0ul;
    db_state_t previous = DB_LEARNING;
    db_config_t config;
    db_detector_t detector;
    db_result_t result;
    int confirmed = 0;

    if (file == NULL) {
        fprintf(stderr, "Cannot open: %s\n", path);
        return 1;
    }
    db_default_config(&config);
    db_init(&detector);
    (void)fgets(line, (int)sizeof(line), file); /* CSV header */

    while (fgets(line, (int)sizeof(line), file) != NULL) {
        float temperature;
        uint8_t power;
        if (!parse_sample(line, &temperature, &power)) continue;
        result = db_step(&detector, &config, temperature, power, 1u);
        row++;
        if (result.state != previous) {
            printf("  row=%-5lu T=%6.2f Pavg=%4.1f rate=%6.2f norm=%6.2f score=%4.2f => %s\n",
                   row, (double)temperature, (double)result.mean_power,
                   (double)result.rise_rate_c_min,
                   (double)result.normalized_rate_c_min_p10,
                   (double)result.score, state_name(result.state));
            previous = result.state;
        }
        if (result.state == DB_DRY_CONFIRMED) confirmed = 1;
    }
    fclose(file);
    printf("%s: %lu rows, result=%s\n", path, row,
           confirmed ? "DRY BOIL" : "NO DRY BOIL");
    return 0;
}

int main(int argc, char **argv)
{
    int i;
    int failed = 0;
    if (argc < 2) {
        printf("Usage: DryBoilAnalyzerC.exe <csv> [more csv files]\n");
        return 2;
    }
    for (i = 1; i < argc; ++i) failed |= replay(argv[i]);
    return failed;
}
