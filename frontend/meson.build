if target_machine.system() != 'emscripten'
faac = executable('faac',
    ['main.c', 'input.c', 'mp4write.c', 'utils.c', 'input.h', 'mp4write.h', 'utils.h'],
    include_directories: ['..', '../include'],
    link_with: libfaac,
    install: true
)
endif

if target_machine.system() == 'windows'
    windows = import('windows')
    resource_src = windows.compile_resources('faacgui.rc', 'icon.rc')
    faacgui = executable('faacgui',
        ['input.c', 'input.h', 'maingui.c', 'utils.c', 'mp4write.c', 'resource.h', 'utils.h', 'mp4write.h'] + resource_src,
        include_directories: ['..', '../include'],
        link_with: libfaac,
        win_subsystem: 'windows',
        install: true
    )
endif

if target_machine.system() == 'emscripten'
    faac_wasm = executable('faac-wasm',
        ['wasm.c', 'utils.c', 'mp4write.c', 'utils.h', 'mp4write.h'],
        include_directories: ['..', '../include'],
        link_with: libfaac,
        name_suffix: 'js',
        install: true
    )
endif
