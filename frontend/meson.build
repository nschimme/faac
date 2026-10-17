# Only charset.c uses iconv, so keep detection scoped here, not project-wide.
libiconv = cc.find_library('iconv', required: false)
has_iconv = cc.has_header('iconv.h') and cc.has_function('iconv_open', dependencies: [libiconv])

# Reruns every build, unlike config.h; only charset.c needs it, so
# it's scoped here, not the root meson.build. No --tags: release tags here
# are lightweight, so this always resolves to the bare short hash rather
# than a tag name -- avoids reconciling it against meson.project_version().
git_version_h = vcs_tag(
    input: 'git_version.h.in',
    output: 'git_version.h',
    replace_string: '@GIT_VERSION@',
    command: ['git', 'describe', '--always', '--dirty'],
    fallback: '',
)

# Large-file semantics on 32-bit POSIX for every fopen(), stat() and seek in the frontends: without
# them a file past 2 GiB cannot be opened.
c_args_list = (has_iconv ? ['-DHAVE_ICONV'] : []) + ['-D_FILE_OFFSET_BITS=64']

libshell32 = cc.find_library('shell32', required: target_machine.system() == 'windows')
libfaam_for_frontend = get_option('frontend-static') ? libfaam_static : libfaam_lib

frontend_common = static_library('frontend_common',
    ['input.c', 'cli_io.c', 'charset.c', 'help.c',
     'input.h', 'cli_io.h', 'charset.h', 'help.h', git_version_h],
    include_directories: ['..', '../include', '../common'],
    c_args: c_args_list,
    dependencies: [libiconv],
)
frontend_common_dep = declare_dependency(
    link_with: [frontend_common, libfaam_for_frontend],
    include_directories: include_directories('..', '../include', '../common'),
    compile_args: c_args_list,
)

if get_option('encoder')
    libfaac_for_frontend = get_option('frontend-static') ? libfaac_static : libfaac
    libfaar_for_frontend = get_option('frontend-static') ? libfaar_static : libfaar
    libfrontend = static_library('frontend',
<<<<<<< HEAD
        ['mp4write.c', 'output.c', 'encode_engine.c', 'progress.c',
         'mp4write.h', 'progress.h', 'output.h', 'encode_engine.h'],
        link_with: [libfaac_for_frontend, libfaar_for_frontend],
        dependencies: [frontend_common_dep, libfaar_dep]
    )

    libm = cc.find_library('m', required: false)
    faac_deps = [libshell32] + (libm.found() ? [libm] : [])
    faac = executable('faac',
        ['main.c'],
        include_directories: ['..', '../include', common_inc],
        link_with: [libfaac_for_frontend, libfaar_for_frontend, libfrontend, libfaam_for_frontend],
        dependencies: faac_deps,
        install: true
    )

    if not meson.is_cross_build()
        rf64_input_test = executable('test_rf64_input',
            ['../tests/rf64_input_test.c', 'input.c', 'cli_io.c', 'charset.c'],
            c_args: c_args_list,
            include_directories: ['..', '../include', common_inc],
            dependencies: [libiconv])
        test('rf64_input_test', find_program('python3'),
            args: [files('../tests/rf64_input_test.py'), rf64_input_test, faac, get_option('max-channels').to_string()])
    endif

    if target_machine.system() == 'windows'
        windows = import('windows')
        resource_src = windows.compile_resources('faacgui.rc', 'icon.rc',
            depend_files: 'faacgui.exe.manifest')
        libcomctl32 = cc.find_library('comctl32', required: true)
        faacgui = executable('faacgui',
            ['maingui.c', 'resource.h'] + resource_src,
            c_args: c_args_list,
            include_directories: ['..', '../include', common_inc],
            link_with: [libfaac_for_frontend, libfrontend, libfaam_for_frontend],
            dependencies: [libcomctl32],
            win_subsystem: 'windows',
            install: true
        )
    endif
endif

if get_option('decoder')
    libfaad_for_frontend = get_option('frontend-static') ? libfaad_static : libfaad
    gc_args = []
    if cc.has_link_argument('-Wl,--gc-sections')
        gc_args += ['-Wl,--gc-sections']
    elif cc.has_link_argument('-Wl,-dead_strip')
        gc_args += ['-Wl,-dead_strip']
    endif
    faad = executable('faad',
        ['faad_main.c', 'mp4read.c', 'adts.c'],
        link_with: [libfaad_for_frontend],
        link_args: gc_args,
        dependencies: [libshell32, frontend_common_dep],
        install: true
    )
    if not meson.is_cross_build()
        pcm_writer_test = executable('test_faad_pcm_writer',
            ['../tests/faad_pcm_writer_test.c', 'mp4read.c', 'adts.c', 'cli_io.c', 'charset.c', 'help.c', git_version_h],
            include_directories: ['..', '../include', common_inc],
            link_with: [libfaad_for_frontend, libfaam_for_frontend],
            c_args: c_args_list,
            dependencies: [libshell32, libiconv])
        test('faad_pcm_writer_test', find_program('python3'),
            args: [files('../tests/faad_pcm_writer_test.py'), pcm_writer_test])
    endif
    if get_option('encoder') and not meson.is_cross_build()
        test('faad_large_input_test', find_program('python3'),
            args: [files('../tests/faad_large_input_test.py'), faac, faad, get_option('max-channels').to_string()], timeout: 120)
    endif
endif

if get_option('muxer')
    # Large-file semantics on 32-bit POSIX for every fopen() and stat() in faam, cli_io.c's
    # included: without them a file past 2 GiB cannot be opened, and cli_same_file() could not
    # see it.
    faam = executable('faam',
        ['faam_main.c', 'adts.c', 'annexb.c',
         'adts.h', 'annexb.h'],
        dependencies: [libshell32, frontend_common_dep],
        install: true
    )
    if get_option('encoder') and get_option('decoder') and not meson.is_cross_build()
        test('faam_cli_test', find_program('python3'),
            args: [files('../tests/faam_cli_test.py'), faac, faam, faad, get_option('max-channels').to_string()], timeout: 300)
        drc_probe = executable('faad_drc_probe', '../tests/faad_drc_probe.c',
            include_directories: ['..', '../include', '../libfaad', common_inc],
            c_args: faad_c_args, link_with: libfaad)
        foreach case : ['defaults', 'gain', 'routing', 'excluded', 'order',
                        'normalisation', 'limiter', 'flags', 'malformed', 'validation', 'multiband', 'short', 'dab', 'he', 'hetilt']
            test('faad_drc_' + case + '_test', find_program('python3'),
                args: [files('../tests/faad_drc_test.py'), faac, drc_probe,
                       get_option('decoder-drc').to_string(), get_option('decoder-sbr').to_string(),
                       get_option('decoder-dab').to_string(), case, get_option('max-channels').to_string()])
        endforeach
        test('faad_drc_oversized_fill_test', find_program('python3'),
            args: [files('../tests/faad_drc_test.py'), faac, drc_probe,
                   get_option('decoder-drc').to_string(), get_option('decoder-sbr').to_string(),
                   get_option('decoder-dab').to_string(), 'oversized', get_option('max-channels').to_string()])
        test('faad_frontend_test', find_program('python3'),
            args: [files('../tests/faad_frontend_test.py'), faac, faad, faam,
                   get_option('max-channels').to_string(), get_option('decoder-sbr').to_string(),
                   get_option('decoder-drc').to_string(), get_option('decoder-dab').to_string()])
    endif
endif
