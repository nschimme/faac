/* Shared test scaffolding for caller-owned track lists and indexed chapters. */
#ifndef FAAM_TEST_HELPERS_H
#define FAAM_TEST_HELPERS_H

static inline int32_t test_sink_write(void *user, const void *data, uint32_t size)
{
    (void)data; *(uint64_t *)user += size; return (int32_t)size;
}
static inline bool test_sink_seek(void *user, uint64_t pos)
{
    *(uint64_t *)user = pos; return true;
}
static inline uint64_t test_sink_tell(void *user) { return *(uint64_t *)user; }

/* Exercise track validation through open, where the new API validates content. */
static inline faam_status test_append_track(faam_muxer_config *cfg,
                                           faam_track_config tracks[8],
                                           const faam_track_config *track,
                                           uint32_t *id)
{
    if (cfg->num_tracks >= 8) return FAAM_ERR_INVALID_ARG;
    tracks[cfg->num_tracks] = *track;
    cfg->tracks = tracks;
    cfg->num_tracks++;
    uint64_t pos = 0;
    faam_io io = { sizeof(io), &pos, NULL, test_sink_write, test_sink_seek, test_sink_tell, NULL };
    faam_muxer *m = NULL;
    faam_status st = faam_muxer_open(cfg, &io, &m);
    if (st == FAAM_OK && id) st = faam_muxer_get_track_id(m, cfg->num_tracks - 1, id);
    faam_muxer_close(&m);
    if (st != FAAM_OK) { cfg->num_tracks--; return st; }
    return FAAM_OK;
}

static inline faam_status test_read_chapters(faam_demuxer *d, faam_chapter *chapters,
                                            uint32_t capacity, uint32_t *count)
{
    faam_status st = faam_demuxer_get_num_chapters(d, count);
    if (st != FAAM_OK) return st;
    for (uint32_t i = 0; chapters && i < capacity && i < *count; i++) {
        chapters[i].struct_size = sizeof(chapters[i]);
        st = faam_demuxer_get_chapter(d, i, &chapters[i]);
        if (st != FAAM_OK) return st;
    }
    return FAAM_OK;
}
#endif
