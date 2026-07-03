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

void faac_mp4_finish(faacEncHandle hEncoder, char *faac_id_string)
{
    unsigned char *ascData = NULL;
    unsigned long ascSize = 0;
    char *version_string;

    faacEncGetDecoderSpecificInfo(hEncoder, &ascData, &ascSize);
    mp4_set_decoder_config(ascData, ascSize);
    if (ascData)
        free(ascData);

    version_string = malloc(strlen(faac_id_string) + 6);
    if (version_string)
    {
        strcpy(version_string, "FAAC ");
        strcpy(version_string + 5, faac_id_string);
        mp4_set_encoder(version_string);
        free(version_string);
    }

    mp4_finish();
    mp4_close();
}
