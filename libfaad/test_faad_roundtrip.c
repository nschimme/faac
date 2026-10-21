/* Encoder -> in-tree decoder round trip, checked against properties the format fixes
 * rather than against today's tuning:
 *  - alignment: with the declared encoder and decoder delays removed, a click lands where
 *    it was put in (the gapless contract);
 *  - level: a steady tone keeps its level to within half a scalefactor step (one step is
 *    2^(1/4), about 1.5 dB);
 *  - channel identity: a tone on one channel does not come out on the other;
 *  - silence decodes to silence;
 *  - HE-AAC v1: band-limited noise above the core's reach comes back through SBR.
 * Both sides are this repository's code, so this shows the two agree with each other and
 * with the declared delays, not that either conforms to the standard. */
#include <math.h>
#include <stdio.h>
#include "faac.h"
#include "faad3.h"
#include "../tests/ftest.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    int16_t *pcm;       /* decoded, interleaved */
    size_t frames;      /* per channel */
    uint32_t rate, channels, delay; /* delay: encoder + decoder, samples per channel */
} decoded;

static decoded roundtrip(enum faac_object_type object, uint32_t rate, uint32_t channels, uint32_t bit_rate,
                         const int16_t *in, uint32_t frames)
{
    faac_params p;
    faac_encoder *enc = NULL;
    faac_encoder_info info = { .struct_size = sizeof(info) };
    decoded d = { NULL, 0, 0, 0, 0 };
    FT_REQUIRE(faac_params_init(&p, sizeof(p)) == FAAC_OK);
    p.sample_rate = rate;
    p.num_channels = channels;
    p.object_type = object;
    p.bit_rate = bit_rate;
    p.output_format = FAAC_STREAM_ADTS;
    p.input_format = FAAC_INPUT_16BIT;
    FT_REQUIRE(faac_encoder_open(&p, &enc) == FAAC_OK);
    FT_REQUIRE(faac_encoder_get_info(enc, &info) == FAAC_OK);

    size_t cap = (size_t)info.max_output_bytes * (frames / info.frame_samples + 8) + 8192, len = 0;
    uint8_t *stream = (uint8_t *)malloc(cap);
    FT_REQUIRE(stream);
    for (uint32_t done = 0; done < frames; done += info.frame_samples) {
        uint32_t n = frames - done < info.frame_samples ? frames - done : info.frame_samples, got = 0;
        FT_REQUIRE(faac_encoder_encode(enc, in + (size_t)done * channels, n * channels,
                                       stream + len, (uint32_t)(cap - len), &got) == FAAC_OK);
        len += got;
    }
    for (uint32_t got = 1; got;) {
        FT_REQUIRE(faac_encoder_encode(enc, NULL, 0, stream + len, (uint32_t)(cap - len), &got) == FAAC_OK);
        len += got;
    }
    FT_REQUIRE(faac_encoder_close(&enc) == FAAC_OK);

    faad_config cfg;
    faad_decoder *dec = NULL;
    faad_stream_info si = { .struct_size = sizeof(si) };
    faad_config_init(&cfg, sizeof(cfg));
    cfg.stream_format = FAAD_STREAM_ADTS;
    cfg.output_format = FAAD_OUTPUT_16BIT;
    FT_REQUIRE(faad_decoder_open(&cfg, NULL, 0, &dec) == FAAD_OK);

    size_t out_cap = (size_t)frames * 4 * channels * sizeof(int16_t) + 65536, out_len = 0;
    uint8_t *out = (uint8_t *)malloc(out_cap);
    FT_REQUIRE(out);
    for (size_t pos = 0; pos < len;) {
        uint32_t used = 0, written = 0, flags = 0;
        uint32_t packet = ((uint32_t)(stream[pos + 3] & 3) << 11) | ((uint32_t)stream[pos + 4] << 3) | (stream[pos + 5] >> 5);
        FT_REQUIRE(faad_decoder_get_info(dec, &si) == FAAD_OK);
        FT_REQUIRE(faad_decode_frame(dec, stream + pos, packet, &used, out + out_len,
                                     (uint32_t)(out_cap - out_len), &written, &flags) == FAAD_OK);
        FT_REQUIRE(used == packet);
        out_len += written;
        pos += packet;
    }
    FT_REQUIRE(faad_decoder_get_info(dec, &si) == FAAD_OK);
    d.rate = si.sample_rate;
    d.channels = si.channels;
    d.delay = info.encoder_delay + si.decoder_delay;
    d.pcm = (int16_t *)out;
    d.frames = out_len / sizeof(int16_t) / si.channels;
    FT_REQUIRE(faad_decoder_close(&dec) == FAAD_OK);
    free(stream);
    return d;
}

/* Amplitude of the sinusoid at `hz` over frames [from, to) of one channel (Goertzel). */
static double tone_amplitude(const int16_t *pcm, uint32_t channels, uint32_t channel,
                             size_t from, size_t to, double hz, double rate)
{
    double w = 2.0 * M_PI * hz / rate, c = 2.0 * cos(w), s1 = 0, s2 = 0;
    for (size_t i = from; i < to; i++) {
        double s0 = pcm[i * channels + channel] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return 2.0 * sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / (double)(to - from);
}

static double db(double ratio) { return 20.0 * log10(ratio); }

static int16_t *tones(uint32_t rate, uint32_t channels, uint32_t frames, const double *hz, double amp)
{
    int16_t *pcm = (int16_t *)malloc((size_t)frames * channels * sizeof(*pcm));
    FT_REQUIRE(pcm);
    for (uint32_t i = 0; i < frames; i++)
        for (uint32_t c = 0; c < channels; c++)
            pcm[(size_t)i * channels + c] = hz[c] > 0 ? (int16_t)(amp * sin(2.0 * M_PI * hz[c] * i / rate)) : 0;
    return pcm;
}

static void test_click_alignment(enum faac_object_type object, uint32_t rate, uint32_t bit_rate, const char *name)
{
    uint32_t frames = rate * 2;
    size_t clicks[3] = { rate * 4 / 10, rate * 9 / 10, rate * 14 / 10 };
    int16_t *in = (int16_t *)calloc(frames, sizeof(int16_t));
    FT_REQUIRE(in);
    for (size_t k = 0; k < 3; k++) in[clicks[k]] = 30000;
    decoded d = roundtrip(object, rate, 1, bit_rate, in, frames);
    FT_REQUIRE(d.frames > clicks[2] + d.delay + 2048);
    for (size_t k = 0; k < 3; k++) {
        size_t centre = clicks[k] + d.delay, peak = centre - 512;
        for (size_t i = centre - 512; i < centre + 512; i++)
            if (abs(d.pcm[i]) > abs(d.pcm[peak])) peak = i;
        if (labs((long)peak - (long)centre) > 2) fprintf(stderr, "%s click %zu: ", name, k);
        FT_CHECK(labs((long)peak - (long)centre) <= 2); /* a misdeclared delay is a frame (1024+) off */
    }
    free(d.pcm);
    free(in);
}

static void test_tone_level_and_channels(void)
{
    enum { RATE = 44100, FRAMES = RATE * 2 };
    const double hz[2] = { 440.0, 1000.0 };
    int16_t *in = tones(RATE, 2, FRAMES, hz, 8192.0); /* -12 dBFS */
    decoded d = roundtrip(FAAC_OBJ_LOW, RATE, 2, 96000, in, FRAMES);
    size_t from = RATE / 2, to = RATE + RATE / 2;
    for (uint32_t c = 0; c < 2; c++) {
        double own = tone_amplitude(d.pcm, 2, c, from + d.delay, to + d.delay, hz[c], RATE);
        double other = tone_amplitude(d.pcm, 2, c, from + d.delay, to + d.delay, hz[1 - c], RATE);
        FT_CHECK_NEAR(db(own / 8192.0), 0.0, 0.75); /* half of one 2^(1/4) scalefactor step */
        FT_CHECK(db(own / other) >= 20.0);
    }
    free(d.pcm);
    free(in);
}

static void test_silence(void)
{
    enum { RATE = 44100, FRAMES = RATE };
    int16_t *in = (int16_t *)calloc((size_t)FRAMES * 2, sizeof(int16_t));
    FT_REQUIRE(in);
    decoded d = roundtrip(FAAC_OBJ_LOW, RATE, 2, 64000, in, FRAMES);
    size_t nonzero = 0;
    for (size_t i = 0; i < d.frames * 2; i++) nonzero += d.pcm[i] != 0;
    FT_CHECK_INT(nonzero, 0);
    free(d.pcm);
    free(in);
}

/* Sum of |X(f)|^2 for f in [lo, hi) Hz at 2 Hz steps over frames [from, to) of one channel. */
static double band_energy(const int16_t *pcm, uint32_t channels, uint32_t channel,
                          size_t from, size_t to, double lo, double hi, double rate)
{
    double sum = 0;
    for (double hz = lo; hz < hi; hz += 2.0) {
        double a = tone_amplitude(pcm, channels, channel, from, to, hz, rate);
        sum += a * a;
    }
    return sum;
}

/* A band the core cannot carry at this rate must come back through SBR at about the
 * input level, and must not appear when the input has none. */
static double he_band_energy(int with_band, double *input_energy)
{
    enum { RATE = 48000, FRAMES = RATE * 3, TONES = 200 };
    int16_t *in = (int16_t *)malloc((size_t)FRAMES * 2 * sizeof(int16_t));
    double phase[TONES];
    uint32_t rng = 99;
    FT_REQUIRE(in);
    for (int k = 0; k < TONES; k++) phase[k] = 2.0 * M_PI * (ft_rand(&rng) & 0xFFFF) / 65536.0;
    for (uint32_t i = 0; i < FRAMES; i++) {
        double v = 4000.0 * sin(2 * M_PI * 440.0 * i / RATE);
        for (int k = 0; with_band && k < TONES; k++) /* noise-like band, 10-15 kHz */
            v += 40.0 * sin(2 * M_PI * (10000.0 + 25.0 * k) * i / RATE + phase[k]);
        in[2 * i] = in[2 * i + 1] = (int16_t)v;
    }
    decoded d = roundtrip(FAAC_OBJ_HE_AAC_V1, RATE, 2, 32000, in, FRAMES);
    size_t from = RATE / 2, to = RATE;
    *input_energy = band_energy(in, 2, 0, from, to, 10500, 13500, RATE);
    double out = band_energy(d.pcm, 2, 0, from + d.delay, to + d.delay, 10500, 13500, d.rate);
    free(d.pcm);
    free(in);
    return out;
}

static void test_he_band(void)
{
    double input, unused;
    double with = he_band_energy(1, &input), without = he_band_energy(0, &unused);
    /* Envelope steps are 1.5 or 3 dB and the encoder trims the high band's noise
     * floor and limiter, so this is a coarse gate: it fails if SBR is lost (tens of dB)
     * or applied at the wrong gain (6 dB or more). */
    double level = 10.0 * log10(with / input);
    FT_CHECK(level >= -9.0 && level <= 3.0);
    FT_CHECK(10.0 * log10(with / without) >= 25.0);
}

int main(void)
{
    test_click_alignment(FAAC_OBJ_LOW, 44100, 96000, "LC 44.1k");
    test_click_alignment(FAAC_OBJ_LOW, 48000, 64000, "LC 48k");
    test_click_alignment(FAAC_OBJ_HE_AAC_V1, 48000, 48000, "HE 48k");
    test_tone_level_and_channels();
    test_silence();
    test_he_band();
    return ft_done();
}
