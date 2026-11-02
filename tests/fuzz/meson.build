# Fuzz harnesses. Each is a real executable in every build and replays a
# generated corpus as a "fuzz" suite test, so API and behavior rot shows up
# in ordinary CI. They link the project's own static libraries.
#
# Real fuzzing: build with a sanitizer and coverage instrumentation, and name
# the engine, e.g.
#   CC=clang meson setup build -Db_sanitize=address,undefined -Db_lundef=false \
#       -Dc_args=-fsanitize=fuzzer-no-link -Dfuzzing-engine=-fsanitize=fuzzer
# With no engine, standalone_main.c supplies main() and replays files.

inc_common = ['../../include', '../../common']
fuzz_engine = get_option('fuzzing-engine')

static_libs = {}
if is_variable('libfaad_static')
    static_libs += {'faad': libfaad_static}
endif
if is_variable('libfaac_static')
    static_libs += {'faac': libfaac_static}
endif
if is_variable('libfaam_static')
    static_libs += {'faam': libfaam_static}
endif
if is_variable('libfaar_static')
    static_libs += {'faar': libfaar_static}
endif

frontend_enabled = get_option('frontend')
fe_inc = []
fe_deps = []
fe_args = []
if frontend_enabled
    fe_inc = [frontend_inc]
    fe_deps = [libiconv]
    fe_args = c_args_list
    static_libs += {'fe': frontend_common}
endif
# Seeds are generated into the build tree; the encoder-derived ones need the
# faac CLI.
gen_args = ['@OUTDIR@/corpus', '@OUTPUT@']
gen_deps = []
if is_variable('faac') and frontend_enabled and get_option('encoder') and not meson.is_cross_build()
    gen_args += ['@0@'.format(faac.full_path()), get_option('max-channels').to_string()]
    gen_deps += faac
endif
py = find_program('python3', required: false)
corpus = []
if py.found()
    corpus = custom_target('fuzz_corpus',
        output: 'corpus.stamp',
        command: [py, files('gen_corpus.py'), gen_args],
        depends: gen_deps,
        build_by_default: false)
endif

# name: harness file stem; libs: static libraries it needs (a missing one
# skips it); when: extra gate; max_len: libFuzzer input cap.
harnesses = [
    {'name': 'faad_adts', 'libs': ['faad'],
     'inc': ['../../libfaad']},
    {'name': 'faad_raw960', 'libs': ['faad'],
     'inc': ['../../libfaad']},
    {'name': 'faac_encode', 'libs': ['faac'],
     'inc': ['../../libfaac'], 'max_len': 70000},
    {'name': 'faac_faad_pce', 'libs': ['faac', 'faad'],
     'inc': ['../../libfaac', '../../libfaad'], 'max_len': 70000},
    {'name': 'faam_demux', 'libs': ['faam'],
     'inc': ['../../libfaam'], 'args': faam_args},
    {'name': 'faam_tags', 'libs': ['faam'],
     'inc': ['../../libfaam'], 'args': faam_args},
    {'name': 'faam_roundtrip', 'libs': ['faam'],
     'inc': ['../../libfaam'], 'args': faam_args},
    {'name': 'faam_annexb', 'libs': ['faam'], 'when': frontend_enabled,
     'srcs': ['../../frontend/annexb.c'],
     'inc': ['../../libfaam'] + fe_inc, 'args': faam_args + fe_args,
     'max_len': 300000},
    {'name': 'faad_mp4', 'libs': ['faad', 'faam', 'fe'], 'when': frontend_enabled,
     'srcs': ['../../frontend/mp4read.c'],
     'inc': ['../../libfaad', '../../libfaam'] + fe_inc,
     'args': faam_args + fe_args, 'deps': fe_deps},
    # The harness uses memfd_create().
    {'name': 'frontend_wav', 'libs': ['fe'], 'args': fe_args,
     'when': frontend_enabled and host_machine.system() == 'linux',
     'inc': fe_inc, 'deps': fe_deps,
     'max_len': 4096},
    {'name': 'frontend_adts_header', 'libs': [], 'when': frontend_enabled,
     'srcs': ['../../frontend/adts.c'], 'inc': fe_inc, 'args': fe_args},
    {'name': 'faar_resampler', 'libs': ['faar'],
     'inc': ['../../libfaar'], 'args': libfaar_args},
]

foreach h : harnesses
    runnable = h.get('when', true)
    link_libs = []
    foreach lib : h['libs']
        if static_libs.has_key(lib)
            link_libs += static_libs[lib]
        else
            runnable = false
        endif
    endforeach
    if runnable
        name = 'fuzz_' + h['name']
        max_len = h.get('max_len', 65536)
        srcs = [name + '.c'] + h.get('srcs', [])
        link_args = []
        if fuzz_engine == ''
            srcs += 'standalone_main.c'
        else
            link_args += fuzz_engine
        endif

        exe = executable(name, srcs,
            override_options: ['b_lto=false'],
            include_directories: inc_common + h.get('inc', []),
            c_args: h.get('args', []),
            link_with: link_libs,
            dependencies: [libm] + h.get('deps', []),
            link_args: link_args,
            install: false)
        if py.found()
        test(name, exe,
            args: ['-runs=0', '-max_len=@0@'.format(max_len), '-timeout=10',
                   meson.current_build_dir() / 'corpus' / h['name']],
            depends: corpus, suite: 'fuzz', timeout: 120)
        endif
    endif
endforeach
