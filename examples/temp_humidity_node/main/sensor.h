#pragma once

#include <stdbool.h>

typedef struct {
    float temperature_c;
    float relative_humidity_pct;
} temp_humidity_reading_t;

/** replace with real sensor read later; fixed fake values for now */
bool read_temperature_humidity(temp_humidity_reading_t *out);
