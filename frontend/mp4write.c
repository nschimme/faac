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

static void put_u32(uint32_t val) {
    uint8_t buf[4] = { (val >> 24) & 0xff, (val >> 16) & 0xff, (val >> 8) & 0xff, val & 0xff };
    fwrite(buf, 1, 4, g_fout);
}

static void put_u16(uint16_t val) {
    uint8_t buf[2] = { (val >> 8) & 0xff, val & 0xff };
    fwrite(buf, 1, 2, g_fout);
}

static void put_u8(uint8_t val) { fwrite(&val, 1, 1, g_fout); }

static void put_data(const void *data, size_t size) { fwrite(data, 1, size, g_fout); }

/* ISO-BMFF box framing: every box is [4-byte size][4-byte FourCC][payload].
 * start_atom writes a zero size placeholder and returns its file offset;
 * end_atom seeks back to fill in the real size once the payload is complete. */
static long start_atom(const char *name) {
    long pos = ftell(g_fout);
    put_u32(0); // placeholder
    put_data(name, 4);
    return pos;
}

static void end_atom(long pos) {
    long curr = ftell(g_fout);
    fseek(g_fout, pos, SEEK_SET);
    put_u32((uint32_t)(curr - pos));
    fseek(g_fout, curr, SEEK_SET);
}

static uint32_t get_mp4_time(void) {
    return (uint32_t)time(NULL) + 2082844800U; // 1904 to 1970
}

/* MPEG-4 expandable class descriptor (ISO 14496-1 §8.3.3): the size is encoded
 * with 7 payload bits per byte; the MSB (0x80) is a continuation flag.
 * We always emit 4 size bytes to keep box layout predictable. */
static void put_descriptor(uint8_t tag, uint32_t size) {
    put_u8(tag);
    for (int i = 3; i >= 0; i--)
        put_u8(((size >> (7 * i)) & 0x7f) | (i ? 0x80 : 0));
}

int mp4atom_open(char *name, int over) {
    if (!over && access(name, 0) == 0) return 1;
    if (!(g_fout = fopen(name, "wb"))) return 1;
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
    return 0;
}

int mp4atom_head(void) {
    long ftyp = start_atom("ftyp");
    put_data("M4A ", 4);
    put_u32(0);
    put_data("M4A mp42isom", 12);
    put_u32(0);
    end_atom(ftyp);

    long free_box = start_atom("free");
    end_atom(free_box);

    /* Record payload start offset so mp4atom_tail() can back-patch the mdat
     * box size once all frames have been written and the total is known. */
    mp4config.mdatofs = (uint32_t)ftell(g_fout) + 8;
    put_u32(0);
    put_data("mdat", 4);
    return 0;
}

int mp4atom_frame(uint8_t * buf, int size, int samples) {
    put_data(buf, size);
    mp4config.mdatsize += size;
    mp4config.samples  += samples;
    if ((mp4config.frame.ents + 1) * 2 > mp4config.frame.bufsize) {
        mp4config.frame.bufsize += 0x4000;
        mp4config.frame.data = realloc(mp4config.frame.data, mp4config.frame.bufsize);
    }
    mp4config.frame.data[mp4config.frame.ents++] = (uint16_t)size;
    if (mp4config.buffersize < size) mp4config.buffersize = (uint16_t)size;
    return 0;
}

static void put_tag(const char *name, const char *data) {
    if (!data) return;
    long box      = start_atom(name);
    long data_box = start_atom("data");
    put_u32(1); // type: text
    put_u32(0); // locale
    put_data(data, strlen(data));
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

    mp4config.bitrate.avg = (uint32_t)(8.0 * mp4config.mdatsize * mp4config.samplerate / mp4config.samples);
    if (!mp4config.bitrate.max) mp4config.bitrate.max = mp4config.bitrate.avg;

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
    put_u32(0x001800);               // bufferSizeDB
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
    for (uint32_t i = 0; i < mp4config.frame.ents; i++)
        put_u32(mp4config.frame.data[i]);
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
    put_tag("\xa9" "nam", mp4config.tag.title);
    put_tag("\xa9" "alb", mp4config.tag.album);
    put_tag("\xa9" "day", mp4config.tag.year);
    put_tag("\xa9" "cmt", mp4config.tag.comment);
    end_atom(ilst);
    end_atom(meta);
    end_atom(udta);
    end_atom(moov);
    return 0;
}

int mp4tag_add(const char *name, const char *data) { return 0; }
