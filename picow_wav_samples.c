/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <math.h>
#include <string.h>
#include "hardware/clocks.h"
#include "hardware/structs/clocks.h"
#include "pico/stdlib.h"
#include "pico/audio_i2s.h"
#include "pico/binary_info.h"
#include "cartoon_laser.h"

// so we can directly copy wav header to struct w/o padding issues
#pragma pack(1)

// pins for waveshare Pico Audio
#define PICO_AUDIO_DATA_PIN 26
#define PICO_AUDIO_CLOCK_PIN_BASE 27
bi_decl(bi_3pins_with_names(PICO_AUDIO_DATA_PIN, "I2S DIN", PICO_AUDIO_CLOCK_PIN_BASE, "I2S BCK", PICO_AUDIO_CLOCK_PIN_BASE+1, "I2S LRCK"));

// default from pico playground
// bi_decl(bi_3pins_with_names(PICO_AUDIO_I2S_DATA_PIN, "I2S DIN", PICO_AUDIO_I2S_CLOCK_PIN_BASE, "I2S BCK", PICO_AUDIO_I2S_CLOCK_PIN_BASE+1, "I2S LRCK"));

#define SAMPLES_PER_BUFFER 256

struct wav_header {
    uint32_t riff;
    uint32_t file_size;
    uint32_t file_type; //should be "WAVE"
    uint32_t format_chunk_marker;
    uint32_t format_length;
    uint16_t format; //should be 1 for Pulse Code Modulation (PCM)
    uint16_t num_channels;
    uint32_t sample_rate; // likely 44100
    uint32_t idk; // (sample rate * bits per sample * channels) / 8
    uint16_t idk2; // something else I'm not using
    uint16_t bits_per_sample;
    uint32_t data; // should be "data", marker of the data section
    uint32_t data_size; // size of the data section
};

void parse_wav_header(struct wav_header * header, unsigned char *wav) {
    // memcpy(wav, &header, 44);
    header = (struct wav_header *) wav;

    // validate header is supported format
    char file_type[4];
    sprintf(file_type, "%u", header->file_type);
    assert(strcmp(file_type, "WAVE") == 0); // should always be "WAVE"
    assert(header.format == 1); // we only support PCM
    char data[4];
    sprintf(data, "%u", header->data);
    assert(strcmp(file_type, "data") == 0); // should always be "data"
    // todo - validate num_channels?

    // todo - validate data_size w/ wav length?
}

struct audio_buffer_pool *init_audio(struct wav_header *header) {
    

    audio_format_t audio_format = {
            .format = header->format,
            .sample_freq = header->sample_rate,
            .channel_count = header->num_channels,
    };

    struct audio_buffer_format producer_format = {
            .format = &audio_format,
            .sample_stride = 2 // what is sample stride?
    };

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

    unsigned char *wav_file = Cartoon_Laser_wav;
    unsigned int wav_file_length = Cartoon_Laser_wav_len;
    struct wav_header * header;
    parse_wav_header(header, wav_file);
    struct audio_buffer_pool *ap = init_audio(header);

    uint32_t startPos = 44;
    uint32_t offset = 0;

    uint vol = 128;
    while (true) {
        int c = getchar_timeout_us(0);
        if (c >= 0) {
            if (c == '-' && vol) vol -= 4;
            if ((c == '=' || c == '+') && vol < 255) vol += 4;
            if (c == 'q') break;

            printf("vol = %d      \r", vol);
        }
        struct audio_buffer *buffer = take_audio_buffer(ap, true);
        int16_t *samples = (int16_t *) buffer->buffer->bytes;
        for (uint i = 0; i < buffer->max_sample_count; i++) {
            int16_t current_sample = *((int16_t *)&wav_file[startPos + offset]);
            samples[i] = (vol * current_sample) >> 8u;
            offset++;
            if (offset + startPos >= wav_file_length) offset = 0;
        }
        buffer->sample_count = buffer->max_sample_count;
        give_audio_buffer(ap, buffer);
    }
    puts("\n");
    return 0;
}
