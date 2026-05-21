#include "sensor.h"

#include <stddef.h>

bool read_temperature_humidity(temp_humidity_reading_t *out) {
    if (out == NULL) {
        return false;
    }
    out->temperature_c = 23.4f;
    out->relative_humidity_pct = 55.0f;
    return true;
}
