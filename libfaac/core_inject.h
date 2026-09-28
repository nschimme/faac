/* Probe-only core (AAC quantizer) decision importer. Loads fdk's per-band
 * codebook/scalefactor/M-S decisions from a FAAD_DUMP C-record file and lets
 * the encoder force them in, band by band, on frames whose window layout
 * (short/long family, max_sfb, group count) already happens to match FAAC's
 * own choice. Never built by default; gated entirely on FAAC_CORE_INJECT. */
#ifndef CORE_INJECT_H
#define CORE_INJECT_H

#include "coder.h"

struct CoreInject;

struct CoreInject *CoreInjectLoad(void);
void CoreInjectFree(struct CoreInject *in);
/* Process-wide lazy singleton: loaded once from FAAC_CORE_INJECT on first
 * call, so quantize.c/stereo.c don't need a pointer threaded through every
 * call signature. Returns NULL (cheaply, every call) when unset. */
struct CoreInject *CoreInjectGet(void);

/* is_short: 1 if this channel's block is the short/grouped family.
 * Returns 1 if the donor has this (frame,ch) and its window layout
 * (short-vs-long family, max_sfb, num_groups) matches; else 0 and no fields
 * are written. On a match: *cb is fdk's codebook for `band` (0/1-11/13/14/15,
 * -1 if band >= donor max_sfb*groups), *sf_shape is fdk's scalefactor minus
 * that frame/channel's mean over its own coded bands (only meaningful when
 * *cb is 1-11), *ms is fdk's ms_used flag for that band (only meaningful
 * when band is in the CPE's shared low region). */
int CoreInjectLookup(struct CoreInject *in, int frame, int ch, int band,
                      int is_short, int max_sfb, int num_groups,
                      int *cb, int *sf_shape, int *ms);

unsigned CoreInjectFields(const struct CoreInject *in);
enum { CI_CLASS = 1, CI_SF = 2, CI_MS = 4 };

/* Call once per (frame,ch) whether or not injection changed anything, so the
 * "share of matched frames" stat in the result table is real. */
void CoreInjectNoteFrame(struct CoreInject *in, int matched);
void CoreInjectStats(const struct CoreInject *in, unsigned long *matched, unsigned long *total);

#endif
