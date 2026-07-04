#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <faac.h>
#include "utils.h"
#include "mp4write.h"

int faac_check_version(char **faac_id_string, char **faac_copyright_string)
{
    if (faacEncGetVersion(faac_id_string, faac_copyright_string) != FAAC_CFG_VERSION)
    {
        return 0;
    }
    return 1;
}

char *faac_get_output_filename(const char *input_filename, int container_mp4)
{
    char *aac_file_name;
    const char *ext = container_mp4 ? ".m4a" : ".aac";
    const char *t = strrchr(input_filename, '.');
    int l = t ? (int)(t - input_filename) : (int)strlen(input_filename);

    aac_file_name = malloc(l + 5);
    if (aac_file_name)
    {
        memcpy(aac_file_name, input_filename, l);
        strcpy(aac_file_name + l, ext);
    }
    return aac_file_name;
}

int faac_is_mp4_filename(const char *filename)
{
    const char *ext = strrchr(filename, '.');
    if (ext)
    {
        if (!strcmp(ext, ".m4a") || !strcmp(ext, ".mp4") || !strcmp(ext, ".m4b"))
            return 1;
    }
    return 0;
}

double faac_calc_speed(unsigned long current_sample, unsigned int samplerate, double time_used)
{
    if (time_used <= 0.0)
        return 0.0;

    return ((double)current_sample / (double)samplerate) / time_used;
}

double faac_calc_eta(unsigned long current_sample, unsigned long total_samples, double time_used)
{
    if (current_sample == 0 || time_used <= 0.0)
        return 0.0;

    return time_used * (double)(total_samples - current_sample) / (double)current_sample;
}

int *faac_mk_chan_map(int channels, int center, int lf)
{
    int *map;
    int inpos;
    int outpos;

    if (!center && !lf)
        return NULL;

    if (channels < 3)
        return NULL;

    if (lf > 0)
        lf--;
    else
        lf = channels - 1;      // default AAC position

    if (center > 0)
        center--;
    else
        center = 0;             // default AAC position

    map = malloc(channels * sizeof(map[0]));
    if (!map)
        return NULL;
    memset(map, 0, channels * sizeof(map[0]));

    outpos = 0;
    if ((center >= 0) && (center < channels))
        map[outpos++] = center;

    inpos = 0;
    for (; outpos < (channels - 1); inpos++)
    {
        if (inpos == center)
            continue;
        if (inpos == lf)
            continue;

        map[outpos++] = inpos;
    }
    if (outpos < channels)
    {
        if ((lf >= 0) && (lf < channels))
            map[outpos] = lf;
        else
            map[outpos] = inpos;
    }

    return map;
}

int faac_check_image_header(const char *buf)
{
    if (!strncmp(buf, "\x89\x50\x4E\x47\x0D\x0A\x1A\x0A", 8))
        return 1;               /* PNG */
    else if (!strncmp(buf, "\xFF\xD8\xFF\xE0", 4) ||
             !strncmp(buf, "\xFF\xD8\xFF\xE1", 4))
        return 1;               /* JPEG */
    else if (!strncmp(buf, "GIF87a", 6) || !strncmp(buf, "GIF89a", 6))
        return 1;               /* GIF */
    else
        return 0;
}

#define SETTAG(id, x) if(x) mp4_set_tag(id, x)
void faac_mp4_finish(faacEncHandle hEncoder, char *faac_id_string, faac_metadata_t *metadata)
{
    unsigned char *ascData = NULL;
    unsigned long ascSize = 0;
    char *version_string;

    faacEncGetDecoderSpecificInfo(hEncoder, &ascData, &ascSize);
    mp4_set_decoder_config(ascData, ascSize);

    version_string = malloc(strlen(faac_id_string) + 6);
    if (version_string)
    {
        strcpy(version_string, "FAAC ");
        strcpy(version_string + 5, faac_id_string);
        mp4_set_encoder(version_string);
    }

    if (metadata)
    {
        SETTAG(MP4TAG_ARTIST, metadata->artist);
        SETTAG(MP4TAG_ARTISTSORT, metadata->artist_sort);
        SETTAG(MP4TAG_COMPOSER, metadata->composer);
        SETTAG(MP4TAG_COMPOSERSORT, metadata->composer_sort);
        SETTAG(MP4TAG_TITLE, metadata->title);
        SETTAG(MP4TAG_ALBUM, metadata->album);
        SETTAG(MP4TAG_ALBUMARTIST, metadata->album_artist);
        SETTAG(MP4TAG_ALBUMARTISTSORT, metadata->album_artist_sort);
        SETTAG(MP4TAG_ALBUMSORT, metadata->album_sort);
        SETTAG(MP4TAG_YEAR, metadata->year);
        SETTAG(MP4TAG_COMMENT, metadata->comment);
        if (metadata->track) mp4_set_track(metadata->track, metadata->ntracks);
        if (metadata->disc) mp4_set_disc(metadata->disc, metadata->ndiscs);
        if (metadata->compilation) mp4_set_compilation(metadata->compilation);
        if (metadata->genre_id) mp4_set_genre(metadata->genre_id);
    }

    mp4_finish();
    mp4_close();

    if (ascData)
        free(ascData);
    if (version_string)
        free(version_string);
}
#undef SETTAG

void faac_init_params(faac_params_t *params)
{
    memset(params, 0, sizeof(faac_params_t));
    params->mpeg_version = MPEG4;
    params->object_type = LOW;
    params->use_tns = 0;
    params->joint_mode = JOINT_MIXED;
    params->shortctl = SHORTCTL_NORMAL;
    params->pns_level = 4;
    params->quality = 0;
    params->bitrate = 0;
    params->output_format = ADTS_STREAM;
    params->input_format = FAAC_INPUT_FLOAT;
}

int faac_apply_params(faacEncHandle hEncoder, faac_params_t *params, int channels)
{
    faacEncConfigurationPtr config = faacEncGetCurrentConfiguration(hEncoder);

    config->mpegVersion = params->mpeg_version;
    config->aacObjectType = params->object_type;
    config->useTns = params->use_tns;
    config->useLfe = params->use_lfe;
    config->jointmode = params->joint_mode;
    config->pnslevel = params->pns_level;
    config->shortctl = params->shortctl;
    config->outputFormat = params->output_format;
    config->inputFormat = params->input_format;

    if (params->cutoff > 0)
        config->bandWidth = params->cutoff;

    if (params->quality > 0)
    {
        config->quantqual = params->quality;
        config->bitRate = 0;
    }
    else if (params->bitrate > 0)
    {
        config->bitRate = params->bitrate / channels;
        config->quantqual = 0;
    }

    return faacEncSetConfiguration(hEncoder, config);
}
