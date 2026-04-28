/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma GCC optimize ("O0")

#include <stdio.h>
#include <math.h>
#include <string.h>
#include "hardware/clocks.h"
#include "hardware/structs/clocks.h"
#include "pico/stdlib.h"
#include "pico/audio_i2s.h"
#include "pico/binary_info.h"
#include "cartoon_laser.h"
#include "blow_bottle.h"
#include "ouch.h"

// pins for waveshare Pico Audio
#define PICO_AUDIO_DATA_PIN 26
#define PICO_AUDIO_CLOCK_PIN_BASE 27
bi_decl(bi_3pins_with_names(PICO_AUDIO_DATA_PIN, "I2S DIN", PICO_AUDIO_CLOCK_PIN_BASE, "I2S BCK", PICO_AUDIO_CLOCK_PIN_BASE+1, "I2S LRCK"));

// default from pico playground
// bi_decl(bi_3pins_with_names(PICO_AUDIO_I2S_DATA_PIN, "I2S DIN", PICO_AUDIO_I2S_CLOCK_PIN_BASE, "I2S BCK", PICO_AUDIO_I2S_CLOCK_PIN_BASE+1, "I2S LRCK"));

#define SAMPLES_PER_BUFFER 256

// literally "WAVE" and "data"
#define WAVE_LITERAL 0x45564157
#define DATA_LITERAL 0x61746164

#define BUTTON_LEFT 15
#define BUTTON_RIGHT 14

struct __attribute__((packed)) wav_header {
    uint32_t riff;
    uint32_t file_size;
    uint32_t file_type; //should be "WAVE"
    uint32_t format_chunk_marker;
    uint32_t format_length;
    uint16_t format; //should be 1 for Pulse Code Modulation (PCM)
    uint16_t num_channels;
    uint32_t sample_rate; // likely 44100
    uint32_t byte_rate; // (sample rate * bits per sample * channels) / 8
    uint16_t block_align; // (num_channels * bits_per_sample) / 8
    uint16_t bits_per_sample;
    uint32_t data; // should be "data", marker of the data section
    uint32_t data_size; // size of the data section
};

struct audio_buffer_pool *init_audio(struct wav_header *header) {

    assert(header->file_type == WAVE_LITERAL);
    assert(header->format == AUDIO_BUFFER_FORMAT_PCM_S16);
    assert(header->data == DATA_LITERAL);
    assert(header->sample_rate == 44100);
    assert(header->num_channels == 1 || header->num_channels == 2);
    
    static audio_format_t audio_format;
    audio_format.format = header->format;
    audio_format.sample_freq = header->sample_rate;
    // hardcode to stereo. When mono, playback will be duplicated for both channels. workaround for audio_i2s to support mono/stereo dynamically
    audio_format.channel_count = 2;

    struct audio_buffer_format producer_format;
    producer_format.format = &audio_format;
    producer_format.sample_stride = header->num_channels == 2 ? header->block_align : header->block_align * 2;

    struct audio_buffer_pool *producer_pool = audio_new_producer_pool(&producer_format, 3,
                                                                      SAMPLES_PER_BUFFER); // todo correct size

    struct audio_i2s_config config = {
            .data_pin = PICO_AUDIO_DATA_PIN,
            .clock_pin_base = PICO_AUDIO_CLOCK_PIN_BASE,
            .dma_channel = 0,
            .pio_sm = 0,
    };

    const struct audio_format *output_format = audio_i2s_setup(&audio_format, &config);
    if (!output_format) {
        panic("PicoAudio: Unable to open audio device.\n");
    }

    bool ok = audio_i2s_connect(producer_pool);
    assert(ok);
    audio_i2s_set_enabled(true);

    return producer_pool;
}

int main() {
    stdio_init_all();

    gpio_init(BUTTON_LEFT);
    gpio_set_dir(BUTTON_LEFT, GPIO_IN);
    gpio_pull_up(BUTTON_LEFT);

    gpio_init(BUTTON_RIGHT);
    gpio_set_dir(BUTTON_RIGHT, GPIO_IN);
    gpio_pull_up(BUTTON_RIGHT);

    unsigned char *wav_file = Cartoon_Laser_wav;
    unsigned int wav_file_length = Cartoon_Laser_wav_len;
    struct wav_header * header = (struct wav_header *) wav_file;

    struct audio_buffer_pool *ap = init_audio(header);

    uint32_t startByteOffset = sizeof(header);
    uint32_t currentByteOffset = startByteOffset;
    uint8_t channelStride = header->block_align / header->num_channels;

    uint vol = 128;

    while (true) {
        if (!gpio_get(BUTTON_LEFT) && vol > 0) vol -= 1;
        if (!gpio_get(BUTTON_RIGHT) && vol < 255) vol += 1;
        if (!gpio_get(BUTTON_LEFT) && !gpio_get(BUTTON_RIGHT)) break;

        struct audio_buffer *buffer = take_audio_buffer(ap, true);
        int16_t *samples = (int16_t *) buffer->buffer->bytes;

        for (uint i = 0; i < buffer->max_sample_count; i ++) {
            for (uint channel = 0; channel < header->num_channels; channel++) {
                int16_t current_sample = *((int16_t *)&wav_file[currentByteOffset]);
                samples[i * header->num_channels + channel] = (vol * current_sample) >> 8u;
                if (header->num_channels == 1) {
                    samples[i * header->num_channels + 1] = (vol * current_sample) >> 8u;
                }
                currentByteOffset += channelStride;
            }
            if (currentByteOffset >= wav_file_length) {
                currentByteOffset = startByteOffset;
                sleep_ms(500);
            }
        }
        buffer->sample_count = buffer->max_sample_count;
        give_audio_buffer(ap, buffer);

    }
    puts("\n");
    return 0;
}
