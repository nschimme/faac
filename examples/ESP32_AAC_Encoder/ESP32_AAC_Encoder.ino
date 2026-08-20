/*
 * ESP32 FAAC Audio Encoder Example
 * Demonstrates initializing FAAC encoder, passing PCM audio samples,
 * receiving AAC ADTS output frames, and closing the encoder.
 */

#include <Arduino.h>
#include <faac.h>

faac_encoder *encoder = NULL;
faac_encoder_info enc_info;

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000);
    Serial.println("\n--- FAAC ESP32 Encoder Test ---");

    faac_params params;
    faac_status status = faac_params_init(&params, sizeof(params));
    if (status != FAAC_OK) {
        Serial.printf("faac_params_init failed: %s\n", faac_strerror(status));
        return;
    }

    params.sample_rate = 44100;
    params.num_channels = 2;
    params.input_format = FAAC_INPUT_16BIT;
    params.output_format = FAAC_STREAM_ADTS;
    params.object_type = FAAC_OBJ_LOW;
    params.bit_rate = 64000;

    status = faac_encoder_open(&params, &encoder);
    if (status != FAAC_OK) {
        Serial.printf("faac_encoder_open failed: %s\n", faac_strerror(status));
        return;
    }

    enc_info.struct_size = sizeof(faac_encoder_info);
    status = faac_encoder_get_info(encoder, &enc_info);
    if (status != FAAC_OK) {
        Serial.printf("faac_encoder_get_info failed: %s\n", faac_strerror(status));
        faac_encoder_close(&encoder);
        return;
    }

    Serial.printf("Encoder opened successfully!\n");
    Serial.printf("Frame samples: %u, Max output bytes: %u\n",
                  enc_info.frame_samples, enc_info.max_output_bytes);

    // Allocate PCM input buffer and output buffer
    uint32_t samples_per_frame = enc_info.frame_samples * params.num_channels;
    int16_t *pcm_buf = (int16_t *)malloc(samples_per_frame * sizeof(int16_t));
    uint8_t *out_buf = (uint8_t *)malloc(enc_info.max_output_bytes);

    if (!pcm_buf || !out_buf) {
        Serial.println("Failed to allocate memory for buffers");
        if (pcm_buf) free(pcm_buf);
        if (out_buf) free(out_buf);
        faac_encoder_close(&encoder);
        return;
    }

    // Generate dummy sine wave PCM samples
    for (uint32_t i = 0; i < samples_per_frame; i += 2) {
        int16_t sample = (int16_t)(10000.0f * sinf(2.0f * M_PI * 440.0f * (i / 2) / 44100.0f));
        pcm_buf[i] = sample;     // Left
        pcm_buf[i + 1] = sample; // Right
    }

    // Encode one frame
    uint32_t bytes_written = 0;
    status = faac_encoder_encode(encoder, pcm_buf, samples_per_frame, out_buf, enc_info.max_output_bytes, &bytes_written);
    if (status == FAAC_OK) {
        Serial.printf("Encoded 1 frame: %u bytes written\n", bytes_written);
    } else {
        Serial.printf("faac_encoder_encode failed: %s\n", faac_strerror(status));
    }

    // Flush encoder
    status = faac_encoder_encode(encoder, NULL, 0, out_buf, enc_info.max_output_bytes, &bytes_written);
    Serial.printf("Flushed encoder: %u bytes written\n", bytes_written);

    free(pcm_buf);
    free(out_buf);

    faac_encoder_close(&encoder);
    Serial.println("Encoder closed successfully.");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
