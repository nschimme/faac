/*
 * Unit tests for FAAC ML Audio Shaping Telemetry
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../ml_shaping.h"

int main(void)
{
#if defined(FAAC_TELEMETRY) && FAAC_TELEMETRY
    const char *tmp_csv = "test_unit_telemetry.csv";
    MlShapingInitLogging(tmp_csv);
    assert(MlShapingLoggingEnabled());

    MlShapingTelemetry record;
    record.frame_idx = 42;
    record.channel = 0;
    record.num_channels = 2;
    record.sample_rate = 44100;
    record.bitrate_per_ch = 32000;
    record.block_type = ONLY_LONG_WINDOW;
    record.attack_ratio = 1.25f;
    record.total_energy = 1.5e5f;
    record.sfm = 0.25f;
    record.ms_correlation = 0.82f;
    record.tns_enabled = 1;
    record.tns_order = 6;
    record.tns_gain = 2.1f;
    record.ms_used_bands = 28;
    record.total_bands = 47;

    MlShapingLogTelemetry(&record);
    MlShapingCloseLogging();
    assert(!MlShapingLoggingEnabled());

    FILE *f = fopen(tmp_csv, "r");
    assert(f != NULL);
    char buf[512];
    assert(fgets(buf, sizeof(buf), f) != NULL);
    assert(fgets(buf, sizeof(buf), f) != NULL);
    fclose(f);
    remove(tmp_csv);
    printf("ML telemetry unit tests passed (FAAC_TELEMETRY enabled).\n");
#else
    assert(!MlShapingLoggingEnabled());
    MlShapingInitLogging("dummy.csv");
    assert(!MlShapingLoggingEnabled());
    MlShapingCloseLogging();
    printf("ML telemetry unit tests passed (FAAC_TELEMETRY disabled stub).\n");
#endif
    return 0;
}
