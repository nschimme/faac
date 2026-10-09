# Only charset.c uses iconv, so keep detection scoped here, not project-wide.
libiconv = cc.find_library('iconv', required: false)
has_iconv = cc.has_header('iconv.h') and cc.has_function('iconv_open', dependencies: [libiconv])

# Reruns every build, unlike config.h; only encode_engine.c needs it, so
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
# A program linked to the static libraries must not import their symbols from a DLL.
if get_option('frontend-static') or get_option('default_library') == 'static'
    c_args_list += ['-DFAAC_STATIC', '-DFAAD_STATIC', '-DFAAM_STATIC']
endif

libshell32 = cc.find_library('shell32', required: target_machine.system() == 'windows')
libfaam_for_frontend = get_option('frontend-static') ? libfaam_static : libfaam_lib

if get_option('encoder')
    libfaac_for_frontend = get_option('frontend-static') ? libfaac_static : libfaac

    libfrontend = static_library('frontend',
        ['input.c', 'mp4write.c', 'cli_io.c', 'charset.c', 'output.c', 'encode_engine.c', 'progress.c', 'help.c',
         'input.h', 'mp4write.h', 'cli_io.h', 'charset.h', 'progress.h', 'help.h', 'output.h', 'encode_engine.h', '../common/endian.h', git_version_h],
        include_directories: ['..', '../include', common_inc],
        link_with: [libfaac_for_frontend, libfaam_for_frontend],
        c_args: c_args_list,
        dependencies: [libiconv]
    )

    faac = executable('faac',
        ['main.c'],
        c_args: c_args_list,
        include_directories: ['..', '../include', common_inc],
        link_with: [libfaac_for_frontend, libfrontend, libfaam_for_frontend],
        dependencies: [libshell32],
        install: true
    )

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
        ['faad_main.c', 'mp4read.c', 'adts.c', 'cli_io.c', 'charset.c', 'help.c', 'cli_io.h', git_version_h],
        include_directories: ['..', '../include', common_inc],
        link_with: [libfaad_for_frontend, libfaam_for_frontend],
        c_args: c_args_list,
        link_args: gc_args,
        dependencies: [libshell32, libiconv],
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
        test('faad_frontend_test', find_program('python3'),
            args: [files('../tests/faad_frontend_test.py'), faac, faad,
                   get_option('max-channels').to_string(), get_option('decoder-sbr').to_string()])
    endif
endif

if get_option('muxer')
    # Large-file semantics on 32-bit POSIX for every fopen() and stat() in faam, charset.c's
    # included: without them a file past 2 GiB cannot be opened, and cli_same_file() could not
    # see it.
    faam = executable('faam',
        ['faam_main.c', 'adts.c', 'annexb.c', 'cli_io.c', 'charset.c', 'help.c',
         'adts.h', 'annexb.h', 'cli_io.h', git_version_h],
        include_directories: ['..', '../include', common_inc],
        link_with: [libfaam_for_frontend],
        c_args: c_args_list,
        dependencies: [libshell32, libiconv],
        install: true
    )
    if get_option('encoder') and get_option('decoder') and not meson.is_cross_build()
        test('faam_cli_test', find_program('python3'),
            args: [files('../tests/faam_cli_test.py'), faac, faam, faad], timeout: 300)
    endif
endif
