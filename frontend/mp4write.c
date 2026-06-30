/****************************************************************************
    MP4 output module

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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <io.h>
#define access _access
#define W_OK 2
#else
#include <unistd.h>
#endif

#include "mp4write.h"

mp4config_t mp4config = { 0 };
static uint8_t *g_membuf = NULL;
static size_t g_mempos = 0;
static size_t g_memcap = 0;

static inline void mem_write(const void *data, size_t size) {
    if (g_membuf) {
        if (g_mempos + size > g_memcap) {
            size_t new_cap = (g_memcap + size) * 2;
            void *tmp = realloc(g_membuf, new_cap);
            if (!tmp) {
                free(g_membuf);
                g_membuf = NULL;
                return;
            }
            g_membuf = (uint8_t *)tmp;
            g_memcap = new_cap;
        }
        memcpy(g_membuf + g_mempos, data, size);
        g_mempos += size;
    } else {
        fwrite(data, 1, size, (FILE *)mp4config.fout);
    }
}

static inline void put_u32(uint32_t val) {
    uint8_t buf[4];
    buf[0] = (uint8_t)(val >> 24);
    buf[1] = (uint8_t)(val >> 16);
    buf[2] = (uint8_t)(val >> 8);
    buf[3] = (uint8_t)val;
    mem_write(buf, 4);
}

static inline void put_u16(uint16_t val) {
    uint8_t buf[2];
    buf[0] = (uint8_t)(val >> 8);
    buf[1] = (uint8_t)val;
    mem_write(buf, 2);
}

static inline void put_u8(uint8_t val) { mem_write(&val, 1); }

static inline void put_data(const void *data, size_t size) { mem_write(data, size); }

static inline long start_atom(const char *name) {
    long pos = g_membuf ? (long)g_mempos : ftell((FILE *)mp4config.fout);
    put_u32(0);
    put_data(name, 4);
    return pos;
}

static inline void end_atom(long pos) {
    if (g_membuf) {
        uint32_t size = (uint32_t)(g_mempos - pos);
        g_membuf[pos + 0] = (uint8_t)(size >> 24);
        g_membuf[pos + 1] = (uint8_t)(size >> 16);
        g_membuf[pos + 2] = (uint8_t)(size >> 8);
        g_membuf[pos + 3] = (uint8_t)size;
    } else {
        long curr = ftell((FILE *)mp4config.fout);
        fseek((FILE *)mp4config.fout, pos, SEEK_SET);
        put_u32((uint32_t)(curr - pos));
        fseek((FILE *)mp4config.fout, curr, SEEK_SET);
    }
}

static uint32_t get_mp4_time(void) {
    return (uint32_t)time(NULL) + 2082844800U;
}

static void put_descriptor(uint8_t tag, uint32_t size) {
    uint8_t buf[5];
    buf[0] = tag;
    for (int i = 3; i >= 0; i--)
        buf[4 - i] = ((size >> (7 * i)) & 0x7f) | (i ? 0x80 : 0);
    mem_write(buf, 5);
}

int mp4atom_open(char *name, int over) {
    mp4atom_close();
    memset(&mp4config, 0, sizeof(mp4config));

    if (!over && access(name, 0) == 0) return 1;
    mp4config.fout = fopen(name, "wb");
    if (!mp4config.fout) return 1;
    setvbuf((FILE *)mp4config.fout, NULL, _IOFBF, 1048576);

    mp4config.frame.bufsize = 0x4000;
    mp4config.frame.data = (uint32_t *)malloc(mp4config.frame.bufsize);
    return 0;
}

int mp4atom_head(void) {
    g_mempos = 0;
    g_memcap = 4096;
    g_membuf = (uint8_t *)malloc(g_memcap);

    long ftyp = start_atom("ftyp");
    put_data("M4A ", 4);
    put_u32(0);
    put_data("M4A mp42isom", 12);
    put_u32(0);
    end_atom(ftyp);

    long free_box = start_atom("free");
    end_atom(free_box);

    fwrite(g_membuf, 1, g_mempos, (FILE *)mp4config.fout);
    free(g_membuf);
    g_membuf = NULL;

    mp4config.mdatofs = (uint32_t)ftell((FILE *)mp4config.fout) + 8;
    put_u32(0);
    put_data("mdat", 4);
    return 0;
}

static void put_tag(const char *name, const char *data) {
    if (!data) return;
    long box      = start_atom(name);
    long data_box = start_atom("data");
    put_u32(1);
    put_u32(0);
    put_data(data, strlen(data));
    end_atom(data_box);
    end_atom(box);
}

static void put_tag_u8(const char *name, uint8_t val) {
    long box      = start_atom(name);
    long data_box = start_atom("data");
    put_u32(0x15);
    put_u32(0);
    put_u8(val);
    end_atom(data_box);
    end_atom(box);
}

static void put_tag_genre(uint16_t genre) {
    long box      = start_atom("gnre");
    long data_box = start_atom("data");
    put_u32(0);
    put_u32(0);
    put_u16(genre);
    end_atom(data_box);
    end_atom(box);
}

static void put_tag_index(const char *name, uint16_t num, uint16_t total) {
    long box      = start_atom(name);
    long data_box = start_atom("data");
    put_u32(0);
    put_u32(0);
    put_u16(0);
    put_u16(num);
    put_u16(total);
    put_u16(0);
    end_atom(data_box);
    end_atom(box);
}

static void put_tag_image(const uint8_t *data, int size) {
    long box      = start_atom("covr");
    long data_box = start_atom("data");
    put_u32(0x0d);
    put_u32(0);
    put_data(data, size);
    end_atom(data_box);
    end_atom(box);
}

static void put_tag_ext(const char *mean, const char *name, const char *val) {
    long box      = start_atom("----");
    long mean_box = start_atom("mean");
    put_u32(0);
    put_data(mean, strlen(mean));
    end_atom(mean_box);
    long name_box = start_atom("name");
    put_u32(0);
    put_data(name, strlen(name));
    end_atom(name_box);
    long data_box = start_atom("data");
    put_u32(1);
    put_u32(0);
    put_data(val, strlen(val));
    end_atom(data_box);
    end_atom(box);
}

int mp4atom_tail(void) {
    long pos = ftell((FILE *)mp4config.fout);
    fseek((FILE *)mp4config.fout, mp4config.mdatofs - 8, SEEK_SET);
    put_u32(mp4config.mdatsize + 8);
    fseek((FILE *)mp4config.fout, pos, SEEK_SET);

    mp4config.bitrate.avg = (uint32_t)((uint64_t)8 * mp4config.mdatsize * mp4config.samplerate / mp4config.samples);
    if (!mp4config.bitrate.max) mp4config.bitrate.max = mp4config.bitrate.avg;

    g_mempos = 0;
    g_memcap = 65536 + mp4config.frame.ents * 4;
    g_membuf = (uint8_t *)malloc(g_memcap);

    long moov = start_atom("moov");
    long mvhd = start_atom("mvhd");
    uint32_t now = get_mp4_time();
    put_u32(0); put_u32(now); put_u32(now);
    put_u32(mp4config.samplerate); put_u32(mp4config.samples);
    put_u32(0x00010000); put_u16(0x0100); put_u16(0); put_u32(0); put_u32(0);
    put_u32(0x00010000); put_u32(0); put_u32(0);
    put_u32(0); put_u32(0x00010000); put_u32(0);
    put_u32(0); put_u32(0); put_u32(0x40000000);
    put_u32(0); put_u32(0); put_u32(0); put_u32(0); put_u32(0); put_u32(0);
    put_u32(2);
    end_atom(mvhd);

    long trak = start_atom("trak");
    long tkhd = start_atom("tkhd");
    put_u32(1); put_u32(now); put_u32(now); put_u32(1); put_u32(0);
    put_u32(mp4config.samples); put_u32(0); put_u32(0);
    put_u16(0); put_u16(0); put_u16(0x0100); put_u16(0);
    put_u32(0x00010000); put_u32(0); put_u32(0);
    put_u32(0); put_u32(0x00010000); put_u32(0);
    put_u32(0); put_u32(0); put_u32(0x40000000);
    put_u32(0); put_u32(0);
    end_atom(tkhd);

    long mdia = start_atom("mdia");
    long mdhd = start_atom("mdhd");
    put_u32(0); put_u32(now); put_u32(now);
    put_u32(mp4config.samplerate); put_u32(mp4config.samples);
    put_u16(0); put_u16(0);
    end_atom(mdhd);

    long hdlr = start_atom("hdlr");
    put_u32(0); put_u32(0); put_data("soun", 4);
    put_u32(0); put_u32(0); put_u32(0); put_u8(0);
    end_atom(hdlr);

    long minf = start_atom("minf");
    long smhd = start_atom("smhd");
    put_u32(0); put_u16(0); put_u16(0);
    end_atom(smhd);

    long dinf = start_atom("dinf");
    long dref = start_atom("dref");
    put_u32(0); put_u32(1);
    long url = start_atom("url ");
    put_u32(1);
    end_atom(url);
    end_atom(dref);
    end_atom(dinf);

    long stbl = start_atom("stbl");
    long stsd = start_atom("stsd");
    put_u32(0); put_u32(1);
    long mp4a = start_atom("mp4a");
    put_u8(0); put_u8(0); put_u8(0); put_u8(0); put_u8(0); put_u8(0);
    put_u16(1); put_u32(0); put_u32(0);
    put_u16(mp4config.channels); put_u16(mp4config.bits);
    put_u16(0); put_u16(0); put_u16(mp4config.samplerate); put_u16(0);

    long esds = start_atom("esds");
    put_u32(0);
    put_descriptor(3, 3 + 5 + 13 + 5 + mp4config.asc.size + 5 + 1);
    put_u16(0); put_u8(0);
    put_descriptor(4, 13 + 5 + mp4config.asc.size);
    put_u8(0x40); put_u8(0x15); put_u8(0); put_u8(0x18); put_u8(0);
    put_u32(mp4config.bitrate.max); put_u32(mp4config.bitrate.avg);
    put_descriptor(5, mp4config.asc.size);
    put_data(mp4config.asc.data, mp4config.asc.size);
    put_descriptor(6, 1); put_u8(2);
    end_atom(esds);
    end_atom(mp4a);
    end_atom(stsd);

    long stts = start_atom("stts");
    put_u32(0); put_u32(1);
    put_u32(mp4config.frame.ents); put_u32(mp4config.framesamples);
    end_atom(stts);

    long stsc = start_atom("stsc");
    put_u32(0); put_u32(1); put_u32(1);
    put_u32(mp4config.frame.ents); put_u32(1);
    end_atom(stsc);

    long stsz = start_atom("stsz");
    put_u32(0); put_u32(0); put_u32(mp4config.frame.ents);
    if (mp4config.frame.ents) {
        size_t size = (size_t)mp4config.frame.ents * 4;
        if (g_mempos + size > g_memcap) {
            void *tmp;
            g_memcap = g_mempos + size + 0x4000;
            tmp = realloc(g_membuf, g_memcap);
            if (!tmp) return 0;
            g_membuf = (uint8_t *)tmp;
        }
        uint8_t *p = g_membuf + g_mempos;
        for (uint32_t i = 0; i < mp4config.frame.ents; i++) {
            uint32_t val = mp4config.frame.data[i];
            *p++ = (uint8_t)(val >> 24);
            *p++ = (uint8_t)(val >> 16);
            *p++ = (uint8_t)(val >> 8);
            *p++ = (uint8_t)val;
        }
        g_mempos += size;
    }
    end_atom(stsz);

    long stco = start_atom("stco");
    put_u32(0); put_u32(1); put_u32(mp4config.mdatofs);
    end_atom(stco);

    end_atom(stbl);
    end_atom(minf);
    end_atom(mdia);
    end_atom(trak);

    long udta = start_atom("udta");
    long meta = start_atom("meta");
    put_u32(0);
    long hdlr2 = start_atom("hdlr");
    put_u32(0); put_u32(0); put_data("mdirappl", 8);
    put_u32(0); put_u32(0); put_u8(0);
    end_atom(hdlr2);

    long ilst = start_atom("ilst");
    put_tag("\xa9" "too", mp4config.tag.encoder);
    put_tag("\xa9" "ART", mp4config.tag.artist);
    put_tag("soar",       mp4config.tag.artistsort);
    put_tag("\xa9" "wrt", mp4config.tag.composer);
    put_tag("soco",       mp4config.tag.composersort);
    put_tag("\xa9" "nam", mp4config.tag.title);
    if (mp4config.tag.genre) put_tag_genre((uint16_t)mp4config.tag.genre);
    put_tag("\xa9" "alb", mp4config.tag.album);
    put_tag("aART",       mp4config.tag.albumartist);
    put_tag("soaa",       mp4config.tag.albumartistsort);
    put_tag("soal",       mp4config.tag.albumsort);
    if (mp4config.tag.compilation) put_tag_u8("cpil", mp4config.tag.compilation);
    if (mp4config.tag.trackno) put_tag_index("trkn", (uint16_t)mp4config.tag.trackno, (uint16_t)mp4config.tag.ntracks);
    if (mp4config.tag.discno) put_tag_index("disk", (uint16_t)mp4config.tag.discno, (uint16_t)mp4config.tag.ndiscs);
    put_tag("\xa9" "day", mp4config.tag.year);
    if (mp4config.tag.cover.data) put_tag_image(mp4config.tag.cover.data, mp4config.tag.cover.size);
    put_tag("\xa9" "cmt", mp4config.tag.comment);
    for (int i = 0; i < mp4config.tag.extnum; i++)
        put_tag_ext("faac", mp4config.tag.ext[i].name, mp4config.tag.ext[i].data);
    end_atom(ilst);
    end_atom(meta);
    end_atom(udta);
    end_atom(moov);

    fwrite(g_membuf, 1, g_mempos, (FILE *)mp4config.fout);
    free(g_membuf);
    g_membuf = NULL;
    return 0;
}

int mp4tag_add(const char *name, const char *data) {
    int idx = mp4config.tag.extnum;
    if (idx >= TAGMAX) return -1;
    mp4config.tag.ext[idx].name = name;
    mp4config.tag.ext[idx].data = data;
    mp4config.tag.extnum++;
    return 0;
}
