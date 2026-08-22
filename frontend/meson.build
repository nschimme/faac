libfrontend = static_library('frontend',
    ['input.c', 'mp4write.c', 'charset.c', 'output_path.c', 'encode_engine.c',
     'input.h', 'mp4write.h', 'charset.h', 'progress.h', 'output_path.h', 'encode_engine.h'],
    include_directories: ['..', '../include'],
    link_with: libfaac,
    dependencies: [libiconv]
)

faac = executable('faac',
    ['main.c'],
    include_directories: ['..', '../include'],
    link_with: [libfaac, libfrontend],
    install: true
)

if target_machine.system() == 'windows'
    windows = import('windows')
    resource_src = windows.compile_resources('faacgui.rc', 'icon.rc')
    faacgui = executable('faacgui',
        ['maingui.c', 'resource.h'] + resource_src,
        include_directories: ['..', '../include'],
        link_with: [libfaac, libfrontend],
        win_subsystem: 'windows',
        install: true
    )
endif
