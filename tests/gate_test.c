/* SBR Auto-Gating verification test.
 * Verifies that the encoder selects the optimal object type (LC or HE)
 * across a matrix of sample rates and bitrates.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <faac.h>

void test_point(unsigned long sr, unsigned long bitrate_per_ch, int expected_type)
{
    unsigned long samplesInput, maxBytesOutput;
    faacEncHandle h = faacEncOpen(sr, 1, &samplesInput, &maxBytesOutput);
    if (!h) {
        if (expected_type == -1) return; // Expected failure
        printf("  FAIL: faacEncOpen failed (%lu Hz)\n", sr);
        return;
    }

    faacEncConfigurationPtr cfg = faacEncGetCurrentConfiguration(h);
    cfg->aacObjectType = AAC_AUTO;
    cfg->bitRate = bitrate_per_ch;

    int result = faacEncSetConfiguration(h, cfg);
    if (!result) {
        if (expected_type == -1) {
            faacEncClose(h);
            return; // Correctly rejected
        }
        printf("  FAIL: faacEncSetConfiguration failed (%lu Hz, %lu bps)\n", sr, bitrate_per_ch);
        faacEncClose(h);
        return;
    }

    if (expected_type == -1) {
        printf("  FAIL: Should have rejected HE-AAC but didn't (%lu Hz, %lu bps)\n", sr, bitrate_per_ch);
        exit(1);
    }

    int actual = cfg->aacObjectType;
    printf("  %lu Hz / %5lu bps/ch -> %s (Expected %s)\n",
           sr, bitrate_per_ch,
           actual == HE_AAC ? "HE-AAC" : "LC-AAC",
           expected_type == HE_AAC ? "HE-AAC" : "LC-AAC");

    if (actual != expected_type) {
        printf("  FAIL: Incorrect object type selection!\n");
        exit(1);
    }

    faacEncClose(h);
}

int main(void)
{
    printf("HE-AAC Auto-Gating Matrix Test (Rejection & Curve)\n");

    /* 1. Rejection: 16kHz should ALWAYS fail for HE-AAC auto */
    test_point(16000, 16000, LOW);
    test_point(16000, 24000, LOW);

    /* Explicit rejection for he-aac at low SR */
    unsigned long samplesInput, maxBytesOutput;
    faacEncHandle h = faacEncOpen(16000, 1, &samplesInput, &maxBytesOutput);
    faacEncConfigurationPtr cfg = faacEncGetCurrentConfiguration(h);
    cfg->aacObjectType = HE_AAC;
    if (faacEncSetConfiguration(h, cfg) != 0) {
        printf("  FAIL: Accepted HE-AAC at 16kHz!\n");
        exit(1);
    }
    printf("  16000 Hz / HE-AAC -> Correctly rejected\n");
    faacEncClose(h);

    /* 2. Standard 48kHz: HE-AAC up to ~32kbps/ch (max_he = 48000*0.75 - 4000 = 32000) */
    test_point(48000, 24000, HE_AAC);
    test_point(48000, 32000, HE_AAC);
    test_point(48000, 33000, LOW);

    /* 3. Mid 32kHz: HE-AAC up to ~20kbps/ch (max_he = 32000*0.75 - 4000 = 20000) */
    test_point(32000, 18000, HE_AAC);
    test_point(32000, 20000, HE_AAC);
    test_point(32000, 21000, LOW);

    /* 4. Limits */
    test_point(32000, 15000, HE_AAC);
    test_point(32000, 14000, LOW); // min_he is 15000

    printf("PASS\n");
    return 0;
}
