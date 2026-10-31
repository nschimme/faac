/*
 * Unit tests for FAAC ML Audio Shaping & Feature Extraction
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../ml_shaping.h"
#include "../ml_shaping_model.h"

static void test_ml_tns_admission(void)
{
    /* LFE channels should never admit TNS */
    assert(!MlShapingTnsAdmit(1.5f, 0.2f, 32000, ONLY_LONG_WINDOW, true));

    /* Short windows should never admit TNS */
    assert(!MlShapingTnsAdmit(1.5f, 0.2f, 32000, ONLY_SHORT_WINDOW, false));

    /* High SFM (noise-like spectrum) should reject TNS to save bits */
    assert(!MlShapingTnsAdmit(1.5f, 0.90f, 32000, ONLY_LONG_WINDOW, false));

    /* Low SFM with initial frame (no attack history) lets LPC decide */
    assert(MlShapingTnsAdmit(0.0f, 0.2f, 32000, ONLY_LONG_WINDOW, false));

    /* Starved bitrates (< 24 kbps/ch): requires high attack (>= 1.2f) */
    assert(!MlShapingTnsAdmit(0.6f, 0.2f, 16000, ONLY_LONG_WINDOW, false));
    assert(MlShapingTnsAdmit(1.35f, 0.2f, 16000, ONLY_LONG_WINDOW, false));

    /* Standard bitrates (32 kbps/ch): attack >= 0.5f */
    assert(!MlShapingTnsAdmit(0.40f, 0.2f, 32000, ONLY_LONG_WINDOW, false));
    assert(MlShapingTnsAdmit(0.65f, 0.2f, 32000, ONLY_LONG_WINDOW, false));

    /* High bitrates (64+ kbps/ch): attack >= 0.4f */
    assert(!MlShapingTnsAdmit(0.30f, 0.2f, 64000, ONLY_LONG_WINDOW, false));
    assert(MlShapingTnsAdmit(0.45f, 0.2f, 64000, ONLY_LONG_WINDOW, false));
}

static void test_ml_tns_gain_thresholds(void)
{
    /* Starved bitrates demand higher prediction gain to earn transmission */
    assert(MlShapingTnsGainThreshold(16000) >= 1.55f);

    /* Normal bitrates use standard threshold */
    assert(fabsf(MlShapingTnsGainThreshold(32000) - 1.40f) < 0.05f);

    /* High bitrates allow slightly lower hurdle */
    assert(MlShapingTnsGainThreshold(64000) <= 1.40f);
}

static void test_ml_stereo_bias(void)
{
    /* SBR streams do not apply plain LC stereo bias */
    assert(fabsf(MlShapingStereoMsBias(64000, 0.8f, true) - 1.0f) < 1e-4f);

    /* High correlation at good bitrates biases towards M/S */
    assert(MlShapingStereoMsBias(64000, 0.85f, false) >= 1.5f);

    /* Mid bitrates */
    assert(MlShapingStereoMsBias(32000, 0.5f, false) >= 1.1f);
}

static void test_ml_telemetry_logging(void)
{
    const char *tmp_csv = "/tmp/test_unit_telemetry.csv";
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

    /* Check that file exists and is non-empty */
    FILE *f = fopen(tmp_csv, "r");
    assert(f != NULL);
    char buf[512];
    assert(fgets(buf, sizeof(buf), f) != NULL); /* Header */
    assert(fgets(buf, sizeof(buf), f) != NULL); /* First data row */
    fclose(f);
    remove(tmp_csv);
}

int main(void)
{
    test_ml_tns_admission();
    test_ml_tns_gain_thresholds();
    test_ml_stereo_bias();
    test_ml_telemetry_logging();
    printf("All ML audio shaping unit tests passed.\n");
    return 0;
}
