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

#include <stdint.h>

enum {TAGMAX = 100};

typedef struct
{
    uint32_t samplerate;
    // total sound samples
    uint32_t samples;
    uint32_t channels;
    // sample depth
    uint32_t bits;
    // buffer config
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
    // AudioSpecificConfig data:
    struct
    {
        uint8_t *data;
        unsigned long size;
    } asc;
    uint32_t mdatofs;
    uint32_t mdatsize;

    struct
    {
        // meta fields
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
int mp4atom_frame(uint8_t * bitbuf, int bytesWritten, int frame_samples);
int mp4atom_close(void);
int mp4tag_add(const char *name, const char *data);

#endif
