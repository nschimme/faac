#ifndef UTILS_H
#define UTILS_H

#include <faac.h>
#include "mp4write.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Check if the library version matches the header version */
int faac_check_version(char **faac_id_string, char **faac_copyright_string);

/* Generate output filename from input filename and extension */
char *faac_get_output_filename(const char *input_filename, int container_mp4);

/* Check if filename implies MP4 container */
int faac_is_mp4_filename(const char *filename);

/* Calculate encoding speed factor */
double faac_calc_speed(unsigned long current_sample, unsigned int samplerate, double time_used);

/* Calculate estimated time remaining in seconds */
double faac_calc_eta(unsigned long current_sample, unsigned long total_samples, double time_used);

/* Create channel map for AAC */
int *faac_mk_chan_map(int channels, int center, int lf);

/* Check image header for supported formats (PNG, JPEG, GIF) */
int faac_check_image_header(const char *buf);

/* Metadata structure for MP4 */
typedef struct {
    const char *artist;
    const char *artist_sort;
    const char *title;
    const char *album;
    const char *album_sort;
    const char *album_artist;
    const char *album_artist_sort;
    const char *composer;
    const char *composer_sort;
    const char *year;
    const char *comment;
    int genre_id;
    uint32_t track;
    uint32_t ntracks;
    uint32_t disc;
    uint32_t ndiscs;
    int compilation;
} faac_metadata_t;

/* Write MP4 metadata and finalize the file */
void faac_mp4_finish(faacEncHandle hEncoder, char *faac_id_string, faac_metadata_t *metadata);

/* Shared encoder configuration parameters */
typedef struct {
    int mpeg_version;
    int object_type;
    int use_tns;
    int use_lfe;
    int joint_mode;
    int pns_level;
    int shortctl;
    int cutoff;
    int bitrate;   /* in bps */
    int quality;   /* quantqual */
    int output_format;
    int input_format;
} faac_params_t;

/* Initialize params with recommended defaults */
void faac_init_params(faac_params_t *params);

/* Apply params to encoder instance */
int faac_apply_params(faacEncHandle hEncoder, faac_params_t *params, int channels);

#ifdef __cplusplus
}
#endif

#endif /* UTILS_H */
