libfrontend = static_library('frontend',
    ['input.c', 'mp4write.c', 'charset.c', 'output.c', 'encode_engine.c', 'progress.c',
     'input.h', 'mp4write.h', 'charset.h', 'progress.h', 'output.h', 'encode_engine.h'],
    include_directories: ['..', '../include'],
    link_with: libfaac,
    dependencies: [libiconv]
)

libshell32 = cc.find_library('shell32', required: target_machine.system() == 'windows')

faac = executable('faac',
    ['main.c'],
    include_directories: ['..', '../include'],
    link_with: [libfaac, libfrontend],
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
        include_directories: ['..', '../include'],
        link_with: [libfaac, libfrontend],
        dependencies: [libcomctl32],
        win_subsystem: 'windows',
        install: true
    )
endif
