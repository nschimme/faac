if get_option('decoder')
    # Compile the raw 960-sample fuzz harness in regular builds, without libFuzzer's main.
    static_library('fuzz_faad_raw960', 'fuzz_faad_raw960.c',
        include_directories: ['../../include'], install: false)
endif

# Keep the public-API harness buildable in ordinary configurations;
# run.sh links it with libFuzzer and ASan/UBSan for fuzzing sessions.
static_library('fuzz_faar', 'fuzz_faar.c',
    include_directories: ['../../include'],
    dependencies: [libfaar_dep],
    install: false)
