/* vgmstrip.c -- Strips PC Engine VGM and outputs ASM files
 * suitable for replay.
 *
 * Copyright (C) 2016-2022 MooZ
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include "vgm.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

#define VGM_HEADER_SIZE 0x100

/* Offsets (in bytes) of various infos in the VGM header. */
enum VGM_OFFSET
{
    VGM_ID                 = 0x00,
    VGM_EOF_OFFSET         = 0x04,
    VGM_VERSION_NUMBER     = 0x08,
    VGM_GD3_OFFSET         = 0x14,
    VGM_TOTAL_SAMPLE_COUNT = 0x18,
    VGM_LOOP_OFFSET        = 0x1c,
    VGM_LOOP_SAMPLE_COUNT  = 0x20,
    VGM_DATA_OFFSET        = 0x34,
    VGM_VOLUME_MODIFIER    = 0x7c,
    VGM_LOOP_BASE          = 0x7e,
    VGM_LOOP_MODIFIER      = 0x7f,
    VGM_HUC6280_CLOCK      = 0xa4
};

/* VGM magic id */
static const uint8_t vgm_id[] = { 0x56, 0x67, 0x6d, 0x20 };

static inline uint32_t read_u32(uint8_t *buffer) {
    return   (buffer[3] << 24) | (buffer[2] << 16)
           | (buffer[1] <<  8) | (buffer[0]      );
}

/* Read vgm header and check it's a valid PC Engine tune. */
static int vgm_read_header(FILE *stream, vgm_header *header) {
    uint8_t raw_header[VGM_HEADER_SIZE];
    size_t  count;

    memset(header, 0, sizeof(vgm_header));

    count = fread(raw_header, 1, VGM_HEADER_SIZE, stream);
    if(VGM_HEADER_SIZE != count) {
        fprintf(stderr, "failed to read vgm header : %s\n", strerror(errno));
        return -1;
    }

    if(memcmp(raw_header, vgm_id, 4)) {
        fprintf(stderr, "invalid vgm id\n");
        return -1;
    }
    
    header->version_number = read_u32(raw_header+VGM_VERSION_NUMBER);
    if(header->version_number < 0x161) {
        fprintf(stderr, "invalid version number : %3x\n", header->version_number);
        return -1;
    }
    
    header->huc6280_clock = raw_header[VGM_HUC6280_CLOCK];
    if(0 == header->huc6280_clock) {
        fprintf(stderr, "not a PC Engine vgm!\n");
        return -1;
    }
 
    header->eof_offset = read_u32(raw_header+VGM_EOF_OFFSET);
    header->gd3_offset = read_u32(raw_header+VGM_GD3_OFFSET);
    header->total_sample_count = read_u32(raw_header+VGM_TOTAL_SAMPLE_COUNT);
    header->loop_offset = read_u32(raw_header+VGM_LOOP_OFFSET);
    header->loop_sample_count = read_u32(raw_header+VGM_LOOP_SAMPLE_COUNT);
    header->data_offset = read_u32(raw_header+VGM_DATA_OFFSET);
    header->volume_modifier = raw_header[VGM_VOLUME_MODIFIER];
    header->loop_base = raw_header[VGM_LOOP_BASE];
    header->loop_modifier = raw_header[VGM_LOOP_MODIFIER];

    return 0;
}

/* Read VGM data from file. */
static int vgm_read_data(FILE *in, vgm_song *song) {
    const vgm_header *header = &song->header;
    uint8_t *data;
    size_t count;

    uint32_t data_size = header->gd3_offset - header->data_offset;

    fseek(in, header->data_offset+0x34, SEEK_SET);

    song->size = data_size;
    song->current = 0;
    song->data = NULL;
    
    data = (uint8_t*)malloc(data_size);
    if(data == NULL) {
        fprintf(stderr, "alloc error : %s\n", strerror(errno));
        return -1;
    } 

    count = fread(data, 1, data_size, in);
    if(count != data_size) {
        fprintf(stderr, "failed to read data : %s\n", strerror(errno));
        free(data);
        return -1;
    }

    song->data = data;
    song->size = data_size;
    return 0;
}

/* Load VGM song from a file and check if it is a valid PC Engine song. */
int vgm_read(const char *filename, vgm_song *song) {
    FILE *in;
    int ret;

    song->current = song->size = 0;
    song->data = NULL;

    in = fopen(filename, "rb");
    if(in == NULL) {
        fprintf(stderr, "failed to open %s: %s\n", filename, strerror(errno));
        return 0;
    }

    ret = vgm_read_header(in, &song->header);
    if(ret >= 0) {
        ret = vgm_read_data(in, song);
    }

    fclose(in);
    return (ret >= 0) ? 1 : 0;
}
/* Release VGM song memory. */
void vgm_free(vgm_song *song) {
    if(song->data) {
        free(song->data);
    }
    memset(song, 0, sizeof(vgm_song));
}
/* Reset read pointer. */
void vgm_reset(vgm_song *song) {
    song->current = 0;
}
/* Read a single byte from the VGM song. */
int vgm_read_u8(vgm_song *song, uint8_t  *out) {
    if(song->current >= song->size) {
        return 0;
    }
    *out = song->data[song->current++];
    return 1;
}
/* Read a single word from the VGM song. */
int vgm_read_u16(vgm_song *song, uint16_t  *out) {
    if((song->current+2) >= song->size) {
        return 0;
    }
    *out = (song->data[song->current+1] << 8)
         | song->data[song->current];
    song->current += 2;
    return 1;
}
/* Skip n bytes from the VGM song. */
int vgm_skip(vgm_song *song, size_t n) {
    if((song->current+n) >= song->size) {
        return 0;
    }
    song->current += n;
    return 1;
}
