#ifndef UTILS_H
#define UTILS_H

#include <faac.h>

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

/* Write MP4 metadata and finalize the file */
void faac_mp4_finish(faacEncHandle hEncoder, char *faac_id_string);

#ifdef __cplusplus
}
#endif

#endif /* UTILS_H */
