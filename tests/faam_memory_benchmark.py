#!/usr/bin/env python3
"""Compare FAAM release archives independently of codec cost.

Usage: python3 tests/faam_memory_benchmark.py BASELINE_BUILD CANDIDATE_BUILD
Build both with video/fragmented enabled, identical release settings and asserts.
Reports medians and min/max spread; exit 1 flags >3% slowdown for investigation.
Use --case open --iterations 20 to repeat a noisy case. --custom builds each source tree with
instrumented hooks, exercising allocator-backed growth and reporting allocation
counts, peak live bytes and largest requests (host malloc, not hardware PSRAM).
"""
import argparse
import json
from pathlib import Path
import statistics
import subprocess
import tempfile


def run(cmd):
    return subprocess.check_output([str(x) for x in cmd], text=True)


HOOKS = r'''
#include <stddef.h>
void *bench_alloc(size_t);
void *bench_realloc(void *, size_t);
void bench_free(void *);
#define AllocMemory(n) bench_alloc(n)
#define FreeMemory(p) bench_free(p)
#ifdef FAAM_BENCH_BASELINE
#define AllocMemoryFast(n) bench_alloc(n)
#define FreeMemoryFast(p) bench_free(p)
#define ReallocMemory(p,n) bench_realloc(p,n)
#endif
'''
ALLOC = r'''
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
typedef union { max_align_t align; size_t bytes; } header;
static size_t calls, live, peak, largest;
unsigned bench_rounds;
void bench_reset(void) { if (live) abort(); calls = peak = largest = 0; }
static void account(size_t n) { calls++; live += n; if (live > peak) peak = live; if (n > largest) largest = n; }
void *bench_alloc(size_t n) { header *h = malloc(sizeof(*h) + n); if (!h) return NULL; h->bytes = n; account(n); return h + 1; }
void bench_free(void *p) { if (p) { header *h = (header *)p - 1; live -= h->bytes; free(h); } }
void *bench_realloc(void *p, size_t n) { if (!p) return bench_alloc(n); header *h = (header *)p - 1; size_t old = h->bytes; header *q = realloc(h, sizeof(*h) + n); if (!q) return NULL; live -= old; q->bytes = n; account(n); return q + 1; }
__attribute__((destructor)) static void report(void) { if (live) abort(); fprintf(stderr, "allocations/op=%.2f peak=%zu largest=%zu\n", (double)calls / bench_rounds, peak, largest); }
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('--iterations', type=int, default=10)
    parser.add_argument('--custom', action='store_true')
    parser.add_argument('--case', action='append', choices=['open', 'mux', 'mux_multi', 'mux_fragment_multi', 'demux', 'demux_fragment_multi', 'metadata'])
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    if args.iterations < 1:
        parser.error('--iterations must be positive')
    source = Path(__file__).with_name('faam_memory_bench.c').resolve()
    modes = ['open', 'mux', 'mux_multi', 'mux_fragment_multi', 'demux', 'demux_fragment_multi', 'metadata']
    modes = args.case or modes
    results = {}
    with tempfile.TemporaryDirectory(prefix='faam-bench-') as scratch:
        scratch = Path(scratch)
        (scratch / 'hooks.h').write_text(HOOKS)
        (scratch / 'alloc.c').write_text(ALLOC)
        binaries = []
        for i, build in enumerate([args.baseline.resolve(), args.candidate.resolve()]):
            info = json.loads(run(['meson', 'introspect', '--buildsystem-files', build]))
            root = next(Path(p).parent for p in info if Path(p).name == 'meson.build')
            binary = scratch / str(i)
            cmd = ['cc', '-O3', '-DFAAM_STATIC', '-I' + str(root / 'include'), source]
            if args.custom:
                cmd += ['-DFAAM_BENCH_CUSTOM', '-DFAAM_MUXER_VIDEO', '-DFAAM_MUXER_FRAGMENTED', '-I' + str(root / 'common'), '-include', scratch / 'hooks.h']
                if i == 0:
                    cmd += ['-DFAAM_BENCH_BASELINE']
                cmd += [root / 'libfaam' / p for p in ['mux.c', 'demux.c', 'metadata.c', 'faam_util.c', 'tag.c', 'chapter.c', 'atom_patch.c']]
                cmd += [scratch / 'alloc.c']
            else:
                cmd += [build / 'libfaam/libfaam.a']
            run(cmd + ['-o', binary])
            binaries.append(binary)
        for mode in modes:
            for binary in binaries:
                subprocess.run([binary, mode], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            samples = [[], []]
            metrics = ['', '']
            for iteration in range(args.iterations):
                for i in ([0, 1] if iteration % 2 == 0 else [1, 0]):
                    res = subprocess.run([binaries[i], mode], check=True, text=True, capture_output=True)
                    samples[i].append(float(res.stdout))
                    metrics[i] = res.stderr.strip()
            med = [statistics.median(s) for s in samples]
            change = (med[1] / med[0] - 1) * 100
            results[mode] = dict(baseline=med[0], candidate=med[1], change_pct=change, samples=samples, metrics=metrics)
            print(f'{mode:24s} {med[0]*1000:9.6f} -> {med[1]*1000:9.6f} ms {change:+6.2f}% '
                  f'ranges {[tuple(round(v*1000, 6) for v in (min(s), max(s))) for s in samples]}', flush=True)
            if args.custom:
                print('  ' + ' -> '.join(metrics), flush=True)
    if args.json:
        args.json.write_text(json.dumps(results, indent=2) + '\n')
    return int(any(r['change_pct'] > 3 for r in results.values()))


if __name__ == '__main__':
    raise SystemExit(main())
