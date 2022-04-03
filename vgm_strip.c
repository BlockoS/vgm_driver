/* vgmstrip.c -- Strips PC Engine VGM and outputs ASM files
 * suitable for replay.
 *
 * Copyright (C) 2016-2022 MooZ
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <argparse/argparse.h>

#include "vgm.h"

#ifdef _MSC_VER
#    define strcasecmp _stricmp
#endif // _MSC_VER

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif // PATH_MAX

#define SAMPLES_PER_FRAME 0x2df

/* PCE commands */
enum PCE_VGM_CMD {
    PCE_VGM_FRAME_END = 0x00,
    PCE_VGM_GLOBAL_VOL,
    PCE_VGM_FINE_FREQ,
    PCE_VGM_ROUGH_FREQ,
    PCE_VGM_VOL,
    PCE_VGM_PAN,
    PCE_VGM_WAV,
    PCE_VGM_NOISE_FREQ,
    PCE_VGM_LFO_FREQ,
    PCE_VGM_LFO_CTRL,
    PCE_VGM_SLEEP = 0xe0,
    PCE_VGM_DATA_END = 0xff
};

#define PCE_CHAN_COUNT 6
#define PCE_WAV_BUFFER_SIZE 32

/* 
 * A song track holds all the commands for a given channel.
 */
typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
} pce_track;

typedef uint8_t pce_wav[PCE_WAV_BUFFER_SIZE];

/*
 * A song contains 1 track per channel.
 */
typedef struct {
    pce_track tracks[PCE_CHAN_COUNT];
    int sleep[PCE_CHAN_COUNT];
    size_t loop[PCE_CHAN_COUNT];
} pce_song;

/*
 *
 */
typedef struct {
    pce_wav *wav_buffers;
    size_t wav_count;

    pce_song *songs;
    size_t song_count;

    pce_track *subtracks;
    size_t subtrack_count;
} pce_music;

static void pce_track_free(pce_track *in) {
    if(in->data) {
        free(in->data);
    }
    memset(in, 0, sizeof(pce_track));
}

static void pce_song_free(pce_song *in) {
    for(int i=0; i<PCE_CHAN_COUNT; i++) {
        if(in->tracks[i].data) {
            free(in->tracks[i].data);
        }
    }
    memset(in, 0, sizeof(pce_song));
}

static void pce_musis_free(pce_music *in) {
    if(in->wav_buffers) {
        free(in->wav_buffers);
    }
    if(in->songs) {
        for(int i=0; i<in->song_count; i++) {
            pce_song_free(&in->songs[i]);
        }
        free(in->songs);
    }
    if(in->subtracks) {
        for(int i=0; i<in->subtrack_count; i++) {
            pce_track_free(&in->subtracks[i]);
        }
        free(in->subtracks);
    }
    memset(in, 0, sizeof(pce_music));
}

static int pce_add_wavebuffer(pce_music *out, pce_wav wav) {
    int index;
    for(index = 0; index<out->wav_count; index++) {
        if(memcmp(&wav[0], &out->wav_buffers[index][0], PCE_WAV_BUFFER_SIZE) == 0) {
            break;
        }
    }
    if(index == out->wav_count) {
        pce_wav *tmp = (pce_wav*)realloc(out->wav_buffers, (index+1) * sizeof(pce_wav));
        if(tmp == NULL) {
            fprintf(stderr, "failed to expand wavebuffer.\n");
            return -1;
        }
        out->wav_buffers = tmp;
        memcpy(&out->wav_buffers[index][0], &wav[0], PCE_WAV_BUFFER_SIZE);
        out->wav_count = index+1;
    }
    return index;
}

static int pce_track_add_data(pce_song *out, int ch, const uint8_t* data, int n) {
    pce_track *track = &out->tracks[ch];
    if((track->size + n) >= track->capacity) {
        size_t new_capacity = track->capacity ? (track->capacity * 2) : 8;
        uint8_t *tmp = (uint8_t*)realloc(track->data, new_capacity);
        if(tmp == NULL) {
            fprintf(stderr, "failed to expand track data\n");
            return -1;
        }
        track->data = tmp;
        track->capacity = new_capacity;
    }

    memcpy(&track->data[track->size], data, n);
    track->size += n;
    return n;
}

static int pce_track_add_op(pce_song *out, int ch, uint8_t op) {
    return pce_track_add_data(out, ch, &op, 1);
}

static int pce_track_add_op_data(pce_song *out, int ch, uint8_t op, uint8_t data) {
    uint8_t buffer[2] = {op, data};
    return pce_track_add_data(out, ch, &buffer[0], 2);
}

static int flush_sleep(pce_song *out, int ch) {
    int sleep = out->sleep[ch];
    if(sleep == 0) {
        return 1;
    }
    out->sleep[ch] = 0;

    if(sleep == 1) {
        return pce_track_add_op(out, ch, PCE_VGM_FRAME_END);
    }
    for(; sleep>=256; sleep-=256) {
        pce_track_add_op_data(out, ch, PCE_VGM_SLEEP, 255);
    }
    if(sleep > 0) {
        if(sleep == 1) {
            return pce_track_add_op(out, ch, PCE_VGM_FRAME_END);
        }
        pce_track_add_op_data(out, ch, PCE_VGM_SLEEP, sleep-1);
    }
    return 1;
}

static int pce_convert_song(vgm_song *in, pce_song *song, pce_music *out) {
    int ch = 0;
    pce_wav tmp_wav;
    int tmp_wav_idx = 0;

    uint32_t loop_offset = 0;
    if(in->header.loop_offset) {
        loop_offset = (in->header.loop_offset + 0x1c) - in->header.data_offset - 0x34;
    }
    memset(&song->sleep[0], 0, PCE_CHAN_COUNT * sizeof(int));

    for(in->current=0; in->current<in->size;) {
        uint8_t command;
        if(in->current == loop_offset) {
            for(int i=0; i<PCE_CHAN_COUNT; i++) {
                song->loop[i] = song->tracks[i].size;
            }
        }

        if(!vgm_read_u8(in, &command)) {
            return 0;
        }
        if(command == VGM_HUC6280_CMD) {
            uint8_t reg_index, data;
            if(!vgm_read_u8(in, &reg_index)) {
                return 0;
            }
            if(!vgm_read_u8(in, &data)) {
                return 0;
            }

            if(flush_sleep(song, ch) < 0) {
                return 0;
            }

            // check if we need to end wav upload.
            if(tmp_wav_idx && (reg_index != 6)) {
                int wav_index = pce_add_wavebuffer(out, tmp_wav);
                if(wav_index < 0) {
                    return 0;
                }
                if(pce_track_add_op_data(song, ch, PCE_VGM_WAV, wav_index) < 0) {
                    return 0;
                }
                tmp_wav_idx = 0;
            }

            if(reg_index == 0) {
                // Switch to another channel.
                ch = data;
            } else if(reg_index == 4) {
                // Check if we are about to update a wave buffer.
                if((data & 0xc0) == 0) {
                    memset(&tmp_wav[0], 0, sizeof(pce_wav));
                    tmp_wav_idx = 0;
                } else {
                    // Enable/disable channel or set its volume.
                    if(pce_track_add_op_data(song, ch, reg_index, data) < 0) {
                        return 0;
                    }
                }
            } else if(reg_index == 6) {
                // Copy byte to wave buffer.
                tmp_wav[tmp_wav_idx % PCE_WAV_BUFFER_SIZE] = data;
                tmp_wav_idx++;
            } else {
                // Just copy register and associated data.
                if(pce_track_add_op_data(song, ch, reg_index, data) < 0) {
                    return 0;
                }
            }
        } else{
            if(tmp_wav_idx) {
                int wav_index = pce_add_wavebuffer(out, tmp_wav);
                if(wav_index < 0) {
                    return 0;
                }
                if(pce_track_add_op_data(song, ch, PCE_VGM_WAV, wav_index) < 0) {
                    return 0;
                }
                tmp_wav_idx = 0;
            }

            if(command == VGM_WAIT_CMD) {
                /* determine the number of frames to wait */
                uint16_t samples;
                uint16_t frames;
                if(!vgm_read_u16(in, &samples)) {
                    return 0;
                }
                frames = samples / SAMPLES_PER_FRAME;
                for(int i=0; i<PCE_CHAN_COUNT; i++) {
                    song->sleep[i] += frames;
                }
            } else if(command == VGM_FRAME_END) {
                for(int i=0; i<PCE_CHAN_COUNT; i++) {
                    song->sleep[i]++;
                }
            } else if(command == VGM_DATA_END) {
                for(int i=0;i<PCE_CHAN_COUNT; i++) {
                    if(flush_sleep(song, i) < 0) {
                        return 0;
                    }
                    if(pce_track_add_op(song, i, PCE_VGM_DATA_END) < 0) {
                        return 0;
                    }
                }
                break;
            } else if(command == VGM_DAC_STREAM_SETUP) {                // [todo] ignored atm
                vgm_skip(in, 4);
            } else if(command == VGM_DAC_STREAM_DATA) {                 // [todo] ignored atm
                vgm_skip(in, 4);
            } else if(command == VGM_DAC_STREAM_FREQUENCY) {            // [todo] ignored atm
                vgm_skip(in, 5);
            } else if(command == VGM_DAC_STREAM_START) {
                vgm_skip(in, 10);
            } else if(command == VGM_DAC_STREAM_STOP) {
                vgm_skip(in, 1);
            } else if(command == VGM_DAC_STREAM_START_FAST) {
                vgm_skip(in, 4);
            } else {
                fprintf(stderr, "unsupported command %x (offset: %lx)\n", command, (long int)in->current);
                return 0;
            }
        }
    }

    return 1;
}

static int pce_add_song(vgm_song *in, pce_music *out) {
    pce_song *song;
    song = (pce_song*)realloc(out->songs, (out->song_count+1) * sizeof(pce_song));
    if(song == NULL) {
        fprintf(stderr, "failed to allocate song : %s\n", strerror(errno));
        return 0;
    }
    out->songs = song;
    song += out->song_count;
    ++out->song_count;
    memset(song, 0, sizeof(pce_song));

    return pce_convert_song(in, song, out);
}

static int pce_add_subtrack(vgm_song *in, int ch, pce_music *out) {
    int ret;
    pce_song song;
    memset(&song, 0, sizeof(pce_song));
    ret = pce_convert_song(in, &song, out);
    if(ret < 0) {
        return ret;
    }

    pce_track *track = (pce_track*)realloc(out->subtracks, (out->subtrack_count+1) * sizeof(pce_track));
    if(track == NULL) {
        fprintf(stderr, "failed to add track: %s\n", strerror(errno));
        return 0;
    }
    out->subtracks = track;
    track += out->subtrack_count;
    out->subtrack_count++;

    track->capacity = track->size = song.tracks[ch].size + 2;
    track->data = (uint8_t*)malloc(track->size * sizeof(uint8_t));
    if(track->data == NULL) {
        fprintf(stderr, "failed to add track data: %s\n", strerror(errno));
        return 0;
    }

    // force global volume to subtrack
    track->data[0] = PCE_VGM_GLOBAL_VOL;
    track->data[1] = 0xff;

    memcpy(&track->data[2], song.tracks[ch].data, song.tracks[ch].size * sizeof(uint8_t));

    pce_song_free(&song);
    return 1;
}

typedef struct {
    size_t size;
    size_t total;
    int index;
    int bank;
    const char *directory;
    const char *filename;
    FILE *stream;
} OutputState;

static int output_state_init(OutputState *state, const char *output_directory) {
    char str[PATH_MAX];

    state->bank = 0;
    state->index = 0;
    state->total = 0;
    state->size = 8192;
    state->directory = output_directory;
    state->filename = "music.inc";

    snprintf(str, PATH_MAX, "%s/%s", state->directory, state->filename);
    state->stream = fopen(str, "wb");
    if(state->stream == NULL) {
        fprintf(stderr, "failed to open %s : %s\n", state->filename, strerror(errno));
        return -1;
    }
    return 1;
}

static void output_state_free(OutputState *state) {
    if(state->stream) {
        fclose(state->stream);
        state->stream = NULL;
    }
}

int output_track(pce_track *track, const char *prefix, int index, OutputState *state) {
    char filename[PATH_MAX];
    size_t j = 0;
    int label = 0;

    if(state->size && (state->size < 8192)) {
        if(!label) {
            fprintf(state->stream, "\n%s_%04d:", prefix, index);
            label++;
        }
        snprintf(filename, PATH_MAX, "%s/%s_%04d.bin", state->directory, prefix, state->index);
        fprintf(state->stream, "\n    .incbin \"%s\"", filename);
        
        state->index++;
        size_t n = state->size;
        if(n > track->size) {
            n = track->size;
        }

        FILE *data_stream = fopen(filename, "wb");
        state->total += fwrite(&track->data[0], 1, n, data_stream);
        fclose(data_stream);
        state->size -= n;
        if(state->size == 0) {
            state->size = 8192;
        }
        j += n;
    }

    for(; (j+state->size)<track->size; state->index++) {
        if((state->total % 8192) == 0) { 
            fprintf(state->stream, "\n    .bank vgm_data_bank+$%03x\n    .org vgm_data_addr", state->bank);
            state->bank++;
        }
        if(!label) {
            fprintf(state->stream, "\n%s_%04d:", prefix, index);
            label++;
        }
        snprintf(filename, PATH_MAX, "%s/%s_%04d.bin", state->directory, prefix, state->index);
        fprintf(state->stream, "\n    .incbin \"%s\"", filename);
        
        FILE *data_stream = fopen(filename, "wb");
        state->total += fwrite(&track->data[j], 1, state->size, data_stream);
        fclose(data_stream);
        j += state->size;
        state->size = 8192;
    }

    if(j<track->size) {
        if((state->total % 8192) == 0) { 
            fprintf(state->stream, "\n    .bank vgm_data_bank+$%03x\n    .org vgm_data_addr", state->bank);
            state->bank++;
        }
        if(!label) {
            fprintf(state->stream, "\n%s_%04d:", prefix, index);
            label++;
        }
        snprintf(filename, PATH_MAX, "%s/%s_%04d.bin", state->directory, prefix, state->index);
        fprintf(state->stream, "\n    .incbin \"%s\"", filename);
        
        state->index++;
        state->size = track->size - j;
        FILE *data_stream = fopen(filename, "wb");
        state->total += fwrite(&track->data[j], 1, state->size, data_stream);
        fclose(data_stream);
        state->size = 8192 - state->size;
    }

    return 1;
}

int output_wav(pce_music *in, OutputState *state) {
    fprintf(state->stream, "\nwav:");
    for(size_t i=0; i<in->wav_count; i++) {
        fprintf(state->stream, "\n    .dw wav%02ux", (unsigned int)i);
    }
    for(size_t i=0; i<in->wav_count; i++) {
        fprintf(state->stream, "\nwav%02ux:", (unsigned int)i);
        for(int j=0; j<PCE_WAV_BUFFER_SIZE; ) {
            fprintf(state->stream, "\n    .db $%02x", in->wav_buffers[i][j++]);
            for(int k=1; (k<8) && (j<PCE_WAV_BUFFER_SIZE);k++, j++) {
                fprintf(state->stream, ",$%02x", in->wav_buffers[i][j]);
            }
        }
    }
    return 1;
}

int output_song_infos(pce_music *in, int song_id, OutputState *state) {
    int loop_bank[PCE_CHAN_COUNT], loop_offset[PCE_CHAN_COUNT];

    pce_song *song = &in->songs[song_id];
    FILE *stream = state->stream;
    
    for(int i=0; i<PCE_CHAN_COUNT; i++) {
        loop_bank[i] = song->loop[i] / 8192;
        loop_offset[i] = song->loop[i] % 8192;
    }

    char song_name[PATH_MAX];
    snprintf(song_name, PATH_MAX, "song%02d", song_id);

    fprintf(stream, "\n%s:\n%s.bank:", song_name, song_name);    
    for(int i=0; i<PCE_CHAN_COUNT; i++) {
        fprintf(stream, "\n    .db bank(%s_%04d)", song_name, i);
    }
    fprintf(stream, "\n%s.hi:", song_name);    
    for(int i=0; i<PCE_CHAN_COUNT; i++) {
        fprintf(stream, "\n    .dwh %s_%04d", song_name, i);
    }
    fprintf(stream, "\n%s.lo:", song_name);    
    for(int i=0; i<PCE_CHAN_COUNT; i++) {
        fprintf(stream, "\n    .dwl %s_%04d", song_name, i);
    }

    fprintf(stream, "\n%s_loop.bank:", song_name);    
    for(int i=0; i<PCE_CHAN_COUNT; i++) {
        fprintf(stream, "\n    .db bank(%s_%04d)+$%0x", song_name, i, loop_bank[i]);
    }
    fprintf(stream, "\n%s_loop.hi:", song_name);    
    for(int i=0; i<PCE_CHAN_COUNT; i++) {
        fprintf(stream, "\n    .dwh %s_%04d+$%0x", song_name, i, loop_offset[i]);
    }
    fprintf(stream, "\n%s_loop.lo:", song_name);    
    for(int i=0; i<PCE_CHAN_COUNT; i++) {
        fprintf(stream, "\n    .dwl %s_%04d+$%0x", song_name, i, loop_offset[i]);
    }
    return 0;
}

int output_song_tracks(pce_music *in, int song_id, OutputState *state) {
    pce_song *song = &in->songs[song_id];
    char song_name[PATH_MAX];
    snprintf(song_name, PATH_MAX, "song%02d", song_id);
    for(int i=0; i<PCE_CHAN_COUNT; i++) {
        output_track(&song->tracks[i], song_name, i, state);
    }

    return 0;
} 

int output_subtracks_infos(pce_music *in, OutputState *state) {
    FILE *stream = state->stream;
    fprintf(stream, "\nsubtracks.bank:");
    for(int i=0; i<in->subtrack_count; i++) {
        fprintf(stream, "\n    .db bank(subtrack_%04d)", i);
    }
    fprintf(stream, "\nsubtracks.hi:");    
    for(int i=0; i<in->subtrack_count; i++) {
        fprintf(stream, "\n    .dwh subtrack_%04d", i);
    }
    fprintf(stream, "\nsubtracks.lo:");    
    for(int i=0; i<in->subtrack_count; i++) {
        fprintf(stream, "\n    .dwl subtrack_%04d", i);
    }
    return 1;
}

int output_subtracks(pce_music *in, OutputState *state) {
    int ret = 1;
    for(int i=0; (i<in->subtrack_count) && (ret >= 0); i++) {
        ret = output_track(&in->subtracks[i], "subtrack", i, state);
    }
    return ret;
}

/* main entry point. */
int main(int argc, const char **argv) {
    int err;
    int ret;

    pce_music music;
    memset(&music, 0, sizeof(pce_music));
	
    int subtrack = -1;
    const char *song = NULL;

	struct argparse_option options[] = 
    {
        OPT_HELP(),
        OPT_INTEGER('t', "subtrack", &subtrack, "Process a subtrack (single channel)", NULL, 0, 0),
        OPT_STRING('s', "song", &song, "Process a song", NULL, 0, 0),
        OPT_END(),
    };

	struct argparse argparse;

	argparse_init(&argparse, options, NULL, ARGPARSE_STOP_AT_NON_OPTION);
	argparse_describe(&argparse, "\nvgm_strip : Strip/convert VGM", "  ");
 
    int path_index = 0;
    const char* output_path = "./";

    do {
        subtrack = -1;
        song = NULL;
        argc = argparse_parse(&argparse, argc, argv);
        if((subtrack >= 0) || (song != NULL)) {
            if(subtrack >= 0) {
                vgm_song vgm;
                err = vgm_read(argv[0], &vgm);
                if(err >= 0) {
                    if(!pce_add_subtrack(&vgm, subtrack, &music)) {
                        return EXIT_FAILURE;        
                    }
                    vgm_free(&vgm);
                }
            } 
            if(song != NULL) { 
                vgm_song vgm;
                err = vgm_read(song, &vgm);
                if(err >= 0) {
                    if(!pce_add_song(&vgm, &music)) {
                        return EXIT_FAILURE;        
                    }
                    vgm_free(&vgm);
                }
            }
        } else if(path_index < 1) {
            path_index++;
            output_path = argv[0];
        }
    } while (argc > 1);

    if(argc != 1) {
        fprintf(stderr, "invalid number of arguments!\n");
        argparse_usage(&argparse);
        return EXIT_FAILURE;
    }
    
    output_path = argv[0];
    
    ret = EXIT_FAILURE;

    OutputState state;
    output_state_init(&state, output_path);
    fprintf(state.stream, "\nsong:");
    for(int i=0; i<music.song_count; i++) {
        fprintf(state.stream, "\n    .dw song%02d", i);
    }

    output_subtracks_infos(&music, &state);

    for(int i=0; i<music.song_count; i++) {
        output_song_infos(&music, i, &state);
    }

    output_wav(&music, &state);

    fprintf(state.stream, "\n    .data"); 

    for(int i=0; i<music.song_count; i++) {
        output_song_tracks(&music, i, &state);
    }
    output_subtracks(&music, &state);

    pce_musis_free(&music);
    output_state_free(&state);
    return ret;
}
