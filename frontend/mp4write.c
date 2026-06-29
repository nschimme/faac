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
static FILE *g_fout = NULL;
static uint8_t *g_membuf = NULL;
static size_t g_mempos = 0;
static size_t g_memcap = 0;

static inline void mem_write(const void *data, size_t size) {
    if (g_membuf) {
        if (g_mempos + size > g_memcap) {
            g_memcap = (g_mempos + size) * 2;
            g_membuf = (uint8_t *)realloc(g_membuf, g_memcap);
        }
        if (g_membuf) {
            memcpy(g_membuf + g_mempos, data, size);
            g_mempos += size;
        }
    } else {
        fwrite(data, 1, size, g_fout);
    }
}

static inline void put_u32(uint32_t val) {
    uint32_t be = ((val >> 24) & 0xFF) | ((val >> 8) & 0xFF00) | ((val << 8) & 0xFF0000) | ((val << 24) & 0xFF000000);
    mem_write(&be, 4);
}

static inline void put_u16(uint16_t val) {
    uint16_t be = (uint16_t)((val >> 8) | (val << 8));
    mem_write(&be, 2);
}

static inline void put_u8(uint8_t val) { mem_write(&val, 1); }

static inline void put_data(const void *data, size_t size) { mem_write(data, size); }

/* ISO-BMFF box framing: every box is [4-byte size][4-byte FourCC][payload].
 * start_atom writes a zero size placeholder and returns its file offset;
 * end_atom seeks back to fill in the real size once the payload is complete. */
static long start_atom(const char *name) {
    long pos = g_membuf ? (long)g_mempos : ftell(g_fout);
    put_u32(0); // placeholder
    put_data(name, 4);
    return pos;
}

static void end_atom(long pos) {
    if (g_membuf) {
        uint32_t size = (uint32_t)(g_mempos - pos);
        g_membuf[pos + 0] = (uint8_t)(size >> 24);
        g_membuf[pos + 1] = (uint8_t)(size >> 16);
        g_membuf[pos + 2] = (uint8_t)(size >> 8);
        g_membuf[pos + 3] = (uint8_t)size;
    } else {
        long curr = ftell(g_fout);
        fseek(g_fout, pos, SEEK_SET);
        uint8_t buf[4] = { (uint8_t)((curr - pos) >> 24), (uint8_t)((curr - pos) >> 16), (uint8_t)((curr - pos) >> 8), (uint8_t)(curr - pos) };
        fwrite(buf, 1, 4, g_fout);
        fseek(g_fout, curr, SEEK_SET);
    }
}

static uint32_t get_mp4_time(void) {
    return (uint32_t)time(NULL) + 2082844800U; // 1904 to 1970
}

/* MPEG-4 expandable class descriptor (ISO 14496-1 §8.3.3): the size is encoded
 * with 7 payload bits per byte; the MSB (0x80) is a continuation flag.
 * We always emit 4 size bytes to keep box layout predictable. */
static void put_descriptor(uint8_t tag, uint32_t size) {
    uint8_t buf[5];
    buf[0] = tag;
    for (int i = 3; i >= 0; i--)
        buf[4 - i] = ((size >> (7 * i)) & 0x7f) | (i ? 0x80 : 0);
    mem_write(buf, 5);
}

int mp4atom_open(char *name, int over) {
    mp4atom_close();

    mp4config.samplerate = 0;
    mp4config.samples = 0;
    mp4config.channels = 0;
    mp4config.bits = 0;
    mp4config.buffersize = 0;
    memset(&mp4config.bitrate, 0, sizeof(mp4config.bitrate));
    mp4config.framesamples = 0;
    mp4config.mdatofs = 0;
    mp4config.mdatsize = 0;
    mp4config.asc.data = NULL;
    mp4config.asc.size = 0;
    /* Do NOT wipe mp4config.tag as it may have been populated by --tag options */

    if (!over && access(name, 0) == 0) return 1;
    if (!(g_fout = fopen(name, "wb"))) return 1;
    setvbuf(g_fout, NULL, _IOFBF, 1048576);
    mp4config.mdatsize = 0;
    mp4config.frame.bufsize = 0x4000;
    mp4config.frame.data = malloc(mp4config.frame.bufsize);
    mp4config.frame.ents = 0;
    return 0;
}

int mp4atom_close(void) {
    if (g_fout) {
        fclose(g_fout);
        g_fout = NULL;
    }
    if (mp4config.frame.data) {
        free(mp4config.frame.data);
        mp4config.frame.data = NULL;
    }
    if (g_membuf) {
        free(g_membuf);
        g_membuf = NULL;
    }
    return 0;
}

int mp4atom_head(void) {
    g_mempos = 0;
    g_memcap = 1024;
    g_membuf = malloc(g_memcap);

    long ftyp = start_atom("ftyp");
    put_data("M4A ", 4);
    put_u32(0);
    put_data("M4A mp42isom", 12);
    put_u32(0);
    end_atom(ftyp);

    long free_box = start_atom("free");
    end_atom(free_box);

    /* Write out the header and switch back to direct file output */
    fwrite(g_membuf, 1, g_mempos, g_fout);
    free(g_membuf);
    g_membuf = NULL;

    /* Record payload start offset so mp4atom_tail() can back-patch the mdat
     * box size once all frames have been written and the total is known. */
    mp4config.mdatofs = (uint32_t)ftell(g_fout) + 8;
    put_u32(0);
    put_data("mdat", 4);
    return 0;
}

int mp4atom_frame(uint8_t * buf, int size, int samples) {
    fwrite(buf, 1, size, g_fout);
    mp4config.mdatsize += size;
    mp4config.samples  += samples;
    if (mp4config.framesamples < (uint32_t)samples)
        mp4config.framesamples = samples;
    /* Rolling 1-second window for maxBitrate (ESDS DecoderConfigDescriptor field). */
    mp4config.bitrate.size    += size;
    mp4config.bitrate.samples += samples;
    if ((uint32_t)mp4config.bitrate.samples >= mp4config.samplerate) {
        uint32_t br = (uint32_t)((uint64_t)8 * mp4config.bitrate.size
                                 * mp4config.samplerate / mp4config.bitrate.samples);
        if (mp4config.bitrate.max < br)
            mp4config.bitrate.max = br;
        mp4config.bitrate.size    = 0;
        mp4config.bitrate.samples = 0;
    }
    if ((mp4config.frame.ents + 1) * sizeof(uint32_t) > mp4config.frame.bufsize) {
        mp4config.frame.bufsize *= 2;
        mp4config.frame.data = realloc(mp4config.frame.data, mp4config.frame.bufsize);
    }
    mp4config.frame.data[mp4config.frame.ents++] = (uint32_t)size;
    if (mp4config.buffersize < size)
        mp4config.buffersize = (uint16_t)size;
    return 0;
}

static void put_tag(const char *name, const char *data) {
    if (!data)
        return;
    long box      = start_atom(name);
    long data_box = start_atom("data");
    put_u32(1);                        // type: text (UTF-8)
    put_u32(0);                        // locale
    put_data(data, strlen(data));
    end_atom(data_box);
    end_atom(box);
}

/* iTunes numeric tag: uint8 value (e.g. cpil compilation flag). */
static void put_tag_u8(const char *name, uint8_t val) {
    long box      = start_atom(name);
    long data_box = start_atom("data");
    put_u32(0x15);                     // type: uint8
    put_u32(0);                        // locale
    put_u8(val);
    end_atom(data_box);
    end_atom(box);
}

/* iTunes genre tag: single uint16 ID (1-based iTunes genre index). */
static void put_tag_genre(uint16_t genre) {
    long box      = start_atom("gnre");
    long data_box = start_atom("data");
    put_u32(0);                        // type: uint16
    put_u32(0);                        // locale
    put_u16(genre);
    end_atom(data_box);
    end_atom(box);
}

/* iTunes track/disc tag: four uint16 fields (padding, number, total, padding). */
static void put_tag_index(const char *name, uint16_t num, uint16_t total) {
    long box      = start_atom(name);
    long data_box = start_atom("data");
    put_u32(0);                        // type: uint16
    put_u32(0);                        // locale
    put_u16(0);                        // padding
    put_u16(num);
    put_u16(total);
    put_u16(0);                        // padding
    end_atom(data_box);
    end_atom(box);
}

/* iTunes cover art tag: binary image data (JPEG or PNG). */
static void put_tag_image(const uint8_t *data, int size) {
    long box      = start_atom("covr");
    long data_box = start_atom("data");
    put_u32(0x0d);                     // type: image (JPEG/PNG)
    put_u32(0);                        // locale
    put_data(data, size);
    end_atom(data_box);
    end_atom(box);
}

/* iTunes freeform tag (----): mean/name/data triple for custom attributes. */
static void put_tag_ext(const char *mean, const char *name, const char *val) {
    long box      = start_atom("----");
    long mean_box = start_atom("mean");
    put_u32(0);                        // version/flags
    put_data(mean, strlen(mean));
    end_atom(mean_box);
    long name_box = start_atom("name");
    put_u32(0);                        // version/flags
    put_data(name, strlen(name));
    end_atom(name_box);
    long data_box = start_atom("data");
    put_u32(1);                        // type: text (UTF-8)
    put_u32(0);                        // locale
    put_data(val, strlen(val));
    end_atom(data_box);
    end_atom(box);
}

int mp4atom_tail(void) {
    long pos = ftell(g_fout);
    /* Back-patch mdat: seek to the 4-byte size field (8 bytes before payload start)
     * and write payload size + 8 header bytes, then return to write moov. */
    fseek(g_fout, mp4config.mdatofs - 8, SEEK_SET);
    put_u32(mp4config.mdatsize + 8);
    fseek(g_fout, pos, SEEK_SET);

    mp4config.bitrate.avg = (uint32_t)((uint64_t)8 * mp4config.mdatsize * mp4config.samplerate / mp4config.samples);
    if (!mp4config.bitrate.max) mp4config.bitrate.max = mp4config.bitrate.avg;

    /* Buffer the entire moov atom in memory to avoid fseek/ftell flushes */
    g_mempos = 0;
    g_memcap = 65536 + mp4config.frame.ents * 4;
    g_membuf = malloc(g_memcap);

    long moov = start_atom("moov");

    long mvhd = start_atom("mvhd");
    uint32_t now = get_mp4_time();
    put_u32(0);                      // version/flags
    put_u32(now);                    // creation_time
    put_u32(now);                    // modification_time
    put_u32(mp4config.samplerate);   // timescale
    put_u32(mp4config.samples);      // duration
    put_u32(0x00010000);             // preferred_rate: 1.0 (16.16 fixed)
    put_u16(0x0100);                 // preferred_volume: 1.0 (8.8 fixed)
    put_u16(0);                      // reserved
    put_u32(0); put_u32(0);          // reserved
    // 3x3 identity transformation matrix
    put_u32(0x00010000); put_u32(0);          put_u32(0);
    put_u32(0);          put_u32(0x00010000); put_u32(0);
    put_u32(0);          put_u32(0);          put_u32(0x40000000);
    put_u32(0); put_u32(0); put_u32(0);      // pre-defined
    put_u32(0); put_u32(0); put_u32(0);
    put_u32(2);                      // next_track_id
    end_atom(mvhd);

    long trak = start_atom("trak");

    long tkhd = start_atom("tkhd");
    put_u32(1);                      // version/flags: track enabled
    put_u32(now);                    // creation_time
    put_u32(now);                    // modification_time
    put_u32(1);                      // track_id
    put_u32(0);                      // reserved
    put_u32(mp4config.samples);      // duration
    put_u32(0); put_u32(0);          // reserved
    put_u16(0);                      // layer
    put_u16(0);                      // alternate_group
    put_u16(0x0100);                 // volume: 1.0 (8.8 fixed)
    put_u16(0);                      // reserved
    // 3x3 identity transformation matrix
    put_u32(0x00010000); put_u32(0);          put_u32(0);
    put_u32(0);          put_u32(0x00010000); put_u32(0);
    put_u32(0);          put_u32(0);          put_u32(0x40000000);
    put_u32(0); put_u32(0);          // width, height (audio track: 0)
    end_atom(tkhd);

    long mdia = start_atom("mdia");

    long mdhd = start_atom("mdhd");
    put_u32(0);                      // version/flags
    put_u32(now);                    // creation_time
    put_u32(now);                    // modification_time
    put_u32(mp4config.samplerate);   // timescale
    put_u32(mp4config.samples);      // duration
    put_u16(0);                      // language
    put_u16(0);                      // pre-defined
    end_atom(mdhd);

    long hdlr = start_atom("hdlr");
    put_u32(0);                      // version/flags
    put_u32(0);                      // pre-defined
    put_data("soun", 4);             // handler_type
    put_u32(0); put_u32(0); put_u32(0); // reserved
    put_u8(0);                       // name (empty string)
    end_atom(hdlr);

    long minf = start_atom("minf");

    long smhd = start_atom("smhd");
    put_u32(0);                      // version/flags
    put_u16(0);                      // balance
    put_u16(0);                      // reserved
    end_atom(smhd);

    long dinf = start_atom("dinf");
    long dref = start_atom("dref");
    put_u32(0);                      // version/flags
    put_u32(1);                      // entry_count
    long url = start_atom("url ");
    put_u32(1);                      // flags: self-contained
    end_atom(url);
    end_atom(dref);
    end_atom(dinf);

    long stbl = start_atom("stbl");

    long stsd = start_atom("stsd");
    put_u32(0);                      // version/flags
    put_u32(1);                      // entry_count

    long mp4a = start_atom("mp4a");
    /* AudioSampleEntry layout (ISO 14496-12 §12.2): 6 reserved bytes,
     * data_reference_index, 8 reserved bytes, then channel/depth/rate fields. */
    put_u8(0); put_u8(0); put_u8(0); // reserved (6 bytes)
    put_u8(0); put_u8(0); put_u8(0);
    put_u16(1);                      // data_reference_index
    put_u32(0); put_u32(0);          // reserved (8 bytes)
    put_u16(mp4config.channels);     // channelcount
    put_u16(mp4config.bits);         // samplesize
    put_u16(0);                      // pre-defined
    put_u16(0);                      // reserved
    put_u16(mp4config.samplerate);   // samplerate (16.16 fixed, integer part only)
    put_u16(0);

    long esds = start_atom("esds");
    put_u32(0);                      // version/flags
    /* ESDS descriptor tree (ISO 14496-1): ES_Descriptor(3) wraps
     * DecoderConfigDescriptor(4) which wraps DecSpecificInfo(5) containing
     * the AudioSpecificConfig bitstream, plus SLConfigDescriptor(6). */
    put_descriptor(3, 3 + 5 + 13 + 5 + mp4config.asc.size + 5 + 1); // ES_Descriptor
    put_u16(0);                      // ES_ID
    put_u8(0);                       // stream priority
    put_descriptor(4, 13 + 5 + mp4config.asc.size);                  // DecoderConfigDescriptor
    put_u8(0x40);                    // objectTypeIndication: Audio ISO/IEC 14496-3
    put_u8(0x15);                    // streamType: AudioStream
    put_u8(0);                       // bufferSizeDB high byte
    put_u8(0x18);                    // bufferSizeDB mid byte (6144 bytes)
    put_u8(0);                       // bufferSizeDB low byte
    put_u32(mp4config.bitrate.max);  // maxBitrate
    put_u32(mp4config.bitrate.avg);  // avgBitrate
    put_descriptor(5, mp4config.asc.size);                            // DecSpecificInfo
    put_data(mp4config.asc.data, mp4config.asc.size);                 // AudioSpecificConfig
    put_descriptor(6, 1);                                             // SLConfigDescriptor
    put_u8(2);                       // predefined: MP4
    end_atom(esds);
    end_atom(mp4a);
    end_atom(stsd);

    long stts = start_atom("stts");
    put_u32(0);                      // version/flags
    put_u32(1);                      // entry_count
    put_u32(mp4config.frame.ents);   // sample_count
    put_u32(mp4config.framesamples); // sample_delta
    end_atom(stts);

    long stsc = start_atom("stsc");
    put_u32(0);                      // version/flags
    put_u32(1);                      // entry_count (one chunk)
    put_u32(1);                      // first_chunk
    put_u32(mp4config.frame.ents);   // samples_per_chunk
    put_u32(1);                      // sample_description_index
    end_atom(stsc);

    long stsz = start_atom("stsz");
    put_u32(0);                      // version/flags
    put_u32(0);                      // sample_size: 0 = variable
    put_u32(mp4config.frame.ents);   // sample_count
    if (mp4config.frame.ents) {
        size_t size = (size_t)mp4config.frame.ents * 4;
        if (g_membuf) {
            if (g_mempos + size > g_memcap) {
                g_memcap = g_mempos + size + 0x4000;
                g_membuf = realloc(g_membuf, g_memcap);
            }
            if (g_membuf) {
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
        } else {
            for (uint32_t i = 0; i < mp4config.frame.ents; i++) {
                put_u32(mp4config.frame.data[i]);
            }
        }
    }
    end_atom(stsz);

    long stco = start_atom("stco");
    put_u32(0);                      // version/flags
    put_u32(1);                      // entry_count
    put_u32(mp4config.mdatofs);      // chunk_offset[0]: mdat payload start
    end_atom(stco);

    end_atom(stbl);
    end_atom(minf);
    end_atom(mdia);
    end_atom(trak);

    long udta = start_atom("udta");
    long meta = start_atom("meta");
    put_u32(0);                      // version/flags
    long hdlr2 = start_atom("hdlr");
    put_u32(0);                      // version/flags
    put_u32(0);                      // pre-defined
    put_data("mdirappl", 8);         // handler_type
    put_u32(0); put_u32(0);          // reserved
    put_u8(0);                       // name (empty string)
    end_atom(hdlr2);

    long ilst = start_atom("ilst");
    put_tag("\xa9" "too", mp4config.tag.encoder);
    put_tag("\xa9" "ART", mp4config.tag.artist);
    put_tag("soar",       mp4config.tag.artistsort);
    put_tag("\xa9" "wrt", mp4config.tag.composer);
    put_tag("soco",       mp4config.tag.composersort);
    put_tag("\xa9" "nam", mp4config.tag.title);
    if (mp4config.tag.genre)
        put_tag_genre((uint16_t)mp4config.tag.genre);
    put_tag("\xa9" "alb", mp4config.tag.album);
    put_tag("aART",       mp4config.tag.albumartist);
    put_tag("soaa",       mp4config.tag.albumartistsort);
    put_tag("soal",       mp4config.tag.albumsort);
    if (mp4config.tag.compilation)
        put_tag_u8("cpil", mp4config.tag.compilation);
    if (mp4config.tag.trackno)
        put_tag_index("trkn", (uint16_t)mp4config.tag.trackno, (uint16_t)mp4config.tag.ntracks);
    if (mp4config.tag.discno)
        put_tag_index("disk", (uint16_t)mp4config.tag.discno, (uint16_t)mp4config.tag.ndiscs);
    put_tag("\xa9" "day", mp4config.tag.year);
    if (mp4config.tag.cover.data)
        put_tag_image(mp4config.tag.cover.data, mp4config.tag.cover.size);
    put_tag("\xa9" "cmt", mp4config.tag.comment);
    for (int i = 0; i < mp4config.tag.extnum; i++)
        put_tag_ext("faac", mp4config.tag.ext[i].name, mp4config.tag.ext[i].data);
    end_atom(ilst);
    end_atom(meta);
    end_atom(udta);
    end_atom(moov);

    if (g_membuf) {
        fwrite(g_membuf, 1, g_mempos, g_fout);
        free(g_membuf);
        g_membuf = NULL;
    }

    return 0;
}

int mp4tag_add(const char *name, const char *data) {
    int idx = mp4config.tag.extnum;
    if (idx >= TAGMAX)
        return -1;
    mp4config.tag.ext[idx].name = name;
    mp4config.tag.ext[idx].data = data;
    mp4config.tag.extnum++;
    return 0;
}
