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

libshell32 = cc.find_library('shell32', required: target_machine.system() == 'windows')

win_c_args = []
win_link_args = []
faac_dll_name = 'faac-2.dll'

if target_machine.system() == 'windows'
    if get_option('default_library') != 'static'
        faac_dll_name = 'faac-' + meson.project_version().split('.')[0] + '.dll'
        if cc.get_argument_syntax() == 'msvc'
            win_link_args += ['/DELAYLOAD:' + faac_dll_name, 'delayimp.lib']
        elif cc.has_link_argument('-Wl,-delayload,' + faac_dll_name)
            win_link_args += ['-Wl,-delayload,' + faac_dll_name]
        endif
        win_c_args += ['-DFAAC_DYNAMIC_BUILD']
    endif
    win_c_args += ['-DLIBFAAC_DLL_NAME="' + faac_dll_name + '"']
endif

libfrontend = static_library('frontend',
    ['input.c', 'mp4write.c', 'charset.c', 'output.c', 'encode_engine.c', 'progress.c', 'win32_utils.c',
     'input.h', 'mp4write.h', 'charset.h', 'progress.h', 'output.h', 'encode_engine.h', 'endian.h', 'win32_utils.h', git_version_h],
    include_directories: ['..', '../include'],
    link_with: libfaac,
    c_args: (has_iconv ? ['-DHAVE_ICONV'] : []) + win_c_args,
    dependencies: [libiconv]
)

faac = executable('faac',
    ['main.c'],
    include_directories: ['..', '../include'],
    link_with: [libfaac, libfrontend],
    dependencies: [libshell32],
    c_args: win_c_args,
    link_args: win_link_args,
    install: true
)

if target_machine.system() == 'windows'
    windows = import('windows')
    resource_src = windows.compile_resources('faacgui.rc', 'icon.rc',
        depend_files: 'faacgui.exe.manifest')
    libcomctl32 = cc.find_library('comctl32', required: true)
    faacgui = executable('faacgui',
        ['maingui.c', 'resource.h'] + resource_src,
        include_directories: ['..', '../include'],
        link_with: [libfaac, libfrontend],
        dependencies: [libcomctl32],
        c_args: win_c_args,
        link_args: win_link_args,
        win_subsystem: 'windows',
        install: true
    )
endif
