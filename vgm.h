/* vgmstrip.c -- Strips PC Engine VGM and outputs ASM files
 * suitable for replay.
 *
 * Copyright (C) 2016-2022 MooZ
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef VGM_H
#define VGM_H

#include <stdint.h>
#include <stdlib.h>

/* Supported VGM commands */
enum VGM_COMMAND
{
    VGM_HUC6280_CMD           = 0xb9,
    VGM_WAIT_CMD              = 0x61,
    VGM_FRAME_END             = 0x62,
    VGM_DATA_END              = 0x66,
    VGM_DAC_STREAM_SETUP      = 0x90,
    VGM_DAC_STREAM_DATA       = 0x91,
    VGM_DAC_STREAM_FREQUENCY  = 0x92,
    VGM_DAC_STREAM_START      = 0x93,
    VGM_DAC_STREAM_STOP       = 0x94,
    VGM_DAC_STREAM_START_FAST = 0x95
};

/* VGM header. */
typedef struct
{
    uint32_t eof_offset;
    uint32_t version_number;
    /* */
    uint32_t gd3_offset;
    uint32_t total_sample_count;
    uint32_t loop_offset;
    uint32_t loop_sample_count;
    /* */
    uint32_t data_offset;
    /* */
    uint8_t volume_modifier;
    /* */
    uint8_t loop_base;
    uint8_t loop_modifier;
    /* */
    uint32_t huc6280_clock;
} vgm_header;

/* VGM song. */
typedef struct {
    vgm_header header;
    uint8_t *data;
    size_t current;
    size_t size;
} vgm_song;

/* Load VGM song from a file and check if it is a valid PC Engine song. */
int vgm_read(const char *filename, vgm_song *song);
/* Release VGM song memory. */
void vgm_free(vgm_song *song);
/* Reset read pointer. */
void vgm_reset(vgm_song *song);
/* Read a single byte from the VGM song. */
int vgm_read_u8 (vgm_song *song, uint8_t  *out);
/* Read a single word from the VGM song. */
int vgm_read_u16(vgm_song *song, uint16_t  *out);
/* Skip n bytes from the VGM song. */
int vgm_skip(vgm_song *song, size_t n);

#endif // VGM_H