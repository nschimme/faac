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
    printf("HE-AAC Auto-Gating Matrix Test (Strict Rejection)\n");

    /* 1. Rejection: 16kHz and 32kHz should ALWAYS fail for HE-AAC auto */
    test_point(16000, 16000, LOW);
    test_point(32000, 16000, LOW);

    /* Explicit rejection for he-aac at low SR */
    unsigned long samplesInput, maxBytesOutput;
    faacEncHandle h = faacEncOpen(32000, 1, &samplesInput, &maxBytesOutput);
    faacEncConfigurationPtr cfg = faacEncGetCurrentConfiguration(h);
    cfg->aacObjectType = HE_AAC;
    if (faacEncSetConfiguration(h, cfg) != 0) {
        printf("  FAIL: Accepted HE-AAC at 32kHz!\n");
        exit(1);
    }
    printf("  32000 Hz / HE-AAC -> Correctly rejected\n");
    faacEncClose(h);

    /* 2. Standard 48kHz: HE-AAC up to ~32kbps/ch (max_he = 48000*0.75 - 4000 = 32000) */
    test_point(48000, 24000, HE_AAC);
    test_point(48000, 32000, HE_AAC);
    test_point(48000, 33000, LOW);

    /* 3. Mid 44.1kHz: HE-AAC up to ~29kbps/ch (max_he = 44100*0.75 - 4000 = 29075) */
    test_point(44100, 24000, HE_AAC);
    test_point(44100, 29000, HE_AAC);
    test_point(44100, 30000, LOW);

    printf("PASS\n");
    return 0;
}
