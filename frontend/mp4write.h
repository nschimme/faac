/****************************************************************************
    MP4 output declarations

    Copyright (C) 2026 Nils Schimmelmann

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
****************************************************************************/

#ifndef MP4WRITE_H
#define MP4WRITE_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {TAGMAX = 100};

typedef struct
{
    uint32_t samplerate;
    uint32_t samples;
    uint32_t channels;
    uint32_t bits;
    uint16_t buffersize;
    struct {
        uint32_t max;
        uint32_t avg;
        int size;
        int samples;
    } bitrate;
    uint32_t framesamples;
    struct
    {
        uint32_t *data;
        uint32_t ents;
        uint32_t bufsize;
    } frame;
    struct
    {
        uint8_t *data;
        unsigned long size;
    } asc;
    void *fout;
    uint32_t mdatofs;
    uint32_t mdatsize;

    struct
    {
        const char *encoder;
        const char *artist;
        const char *artistsort;
        const char *composer;
        const char *composersort;
        const char *title;
        const char *album;
        const char *albumartist;
        const char *albumartistsort;
        const char *albumsort;
        uint8_t compilation;
        uint32_t trackno;
        uint32_t ntracks;
        uint32_t discno;
        uint32_t ndiscs;
        int genre;
        const char *year;
        struct {
            uint8_t *data;
            int size;
        } cover;
        const char *comment;
        struct {
            const char *name;
            const char *data;
        } ext[TAGMAX];
        int extnum;
    } tag;
} mp4config_t;

extern mp4config_t mp4config;

int mp4atom_open(char *name, int over);
int mp4atom_head(void);
int mp4atom_tail(void);
int mp4tag_add(const char *name, const char *data);
int mp4atom_close(void);

static inline void mp4write_fwrite(const void *ptr, size_t size, size_t nmemb) {
    if (mp4config.fout)
        fwrite(ptr, size, nmemb, (FILE *)mp4config.fout);
}

static inline int mp4_record_frame(uint32_t size, uint32_t samples) {
    mp4config.mdatsize += size;
    mp4config.samples  += samples;
    if (mp4config.framesamples < samples)
        mp4config.framesamples = samples;

    mp4config.bitrate.size    += (int)size;
    mp4config.bitrate.samples += (int)samples;
    if ((uint32_t)mp4config.bitrate.samples >= mp4config.samplerate) {
        uint32_t br = (uint32_t)((uint64_t)8 * mp4config.bitrate.size * mp4config.samplerate / mp4config.bitrate.samples);
        if (mp4config.bitrate.max < br)
            mp4config.bitrate.max = br;
        mp4config.bitrate.size    = 0;
        mp4config.bitrate.samples = 0;
    }

    if (mp4config.frame.ents >= mp4config.frame.bufsize) {
        uint32_t new_cap = mp4config.frame.bufsize ? mp4config.frame.bufsize * 2 : 1024;
        uint32_t *tmp = (uint32_t *)realloc(mp4config.frame.data, new_cap * sizeof(uint32_t));
        if (!tmp) return -1;
        mp4config.frame.data = tmp;
        mp4config.frame.bufsize = new_cap;
    }
    mp4config.frame.data[mp4config.frame.ents++] = size;
    if (mp4config.buffersize < (uint16_t)size)
        mp4config.buffersize = (uint16_t)size;
    return 0;
}
#endif
