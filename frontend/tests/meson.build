test_adts = executable('test_adts', ['test_adts.c', '../adts.c'],
    c_args: c_args_list,
    include_directories: ['..', '../..', '../../common'])
test('test_unit_frontend_adts_header', test_adts)

test_charset = executable('test_charset', ['test_charset.c', '../charset.c', git_version_h],
    c_args: c_args_list,
    include_directories: ['..', '../..'],
    dependencies: [libiconv])
test('test_unit_frontend_charset', test_charset)

test_cli_parse = executable('test_cli_parse', 'test_cli_parse.c',
    c_args: c_args_list,
    include_directories: ['..', '../..', '../../include'],
    dependencies: [frontend_common_dep])
test('test_unit_frontend_cli_parsers', test_cli_parse)

test_cli_same_file = executable('test_cli_same_file', 'test_cli_same_file.c',
    c_args: c_args_list,
    include_directories: ['..', '../..', '../../include'],
    dependencies: [frontend_common_dep])
test('test_unit_frontend_cli_same_file', test_cli_same_file,
    args: [meson.current_build_dir()])

test_cover_art = executable('test_cover_art', 'test_cover_art.c',
    c_args: c_args_list,
    include_directories: ['../../include', common_inc])
test('test_unit_frontend_cover_art', test_cover_art)

test_output = executable('test_output', ['test_output.c', '../output.c'],
    c_args: c_args_list,
    include_directories: ['..', '../..', '../../include'])
test('test_unit_frontend_output_helpers', test_output)

if get_option('muxer') and not meson.is_cross_build()
    test_faam_engine = executable('test_faam_engine',
        ['test_faam_engine.c', '../faam_engine.c', '../adts.c', '../annexb.c'],
        c_args: c_args_list,
        include_directories: ['..', '../..', '../../include', '../../common'],
        dependencies: [frontend_common_dep])
    test('test_unit_frontend_faam_engine', test_faam_engine)
endif

if get_option('encoder') and not meson.is_cross_build()
    test_encode_engine = executable('test_encode_engine', 'test_encode_engine.c',
        c_args: c_args_list,
        include_directories: ['..', '../..', '../../include', common_inc, '../../tests'],
        link_with: [libfaac_for_frontend, libfrontend, libfaam_for_frontend],
        dependencies: [frontend_common_dep])
    test('test_integration_faac_encode_engine', test_encode_engine,
        args: [join_paths(meson.current_build_dir(), 'encode-engine-test.wav'),
               join_paths(meson.current_build_dir(), 'encode-engine-test.aac')])
endif

if get_option('decoder') and not meson.is_cross_build()
    test_decode_engine = executable('test_decode_engine',
        ['test_decode_engine.c', '../decode_engine.c', '../mp4read.c', '../adts.c'],
        c_args: c_args_list,
        include_directories: ['..', '../..', '../../include', common_inc, '../../tests'],
        link_with: [libfaad_for_frontend, libfaam_for_frontend],
        dependencies: [frontend_common_dep])
    test('test_integration_faad_decode_engine', test_decode_engine)
endif

if get_option('encoder') and not meson.is_cross_build()
    rf64_input_test = executable('test_rf64_input',
        ['test_rf64_input.c', '../input.c', '../cli_io.c', '../charset.c'],
        c_args: c_args_list,
        include_directories: ['..', '../..', '../../include', common_inc],
        dependencies: [libiconv])
    test('test_unit_faac_rf64_input', rf64_input_test,
        args: [join_paths(meson.current_build_dir(), 'rf64-input-test.wav')])
    test('test_e2e_faac_rf64', find_program('python3'),
        args: [files('test_faac_rf64_cli.py'), faac, get_option('max-channels').to_string()])
endif

if get_option('decoder') and not meson.is_cross_build()
    pcm_writer_test = executable('test_faad_pcm_writer',
        ['test_faad_pcm_writer.c', '../mp4read.c', '../adts.c', '../cli_io.c', '../cli_parse.c', '../charset.c', '../help.c', git_version_h],
        include_directories: ['..', '../..', '../../include', common_inc],
        link_with: [libfaad_for_frontend, libfaam_for_frontend],
        c_args: c_args_list,
        dependencies: [libshell32, libiconv])
    test('test_unit_faad_pcm_writer', find_program('python3'),
        args: [files('test_faad_pcm_writer.py'), pcm_writer_test])
endif

if get_option('encoder') and get_option('decoder') and not meson.is_cross_build()
    test('test_e2e_faad_large_input', find_program('python3'),
        args: [files('test_faad_large_input.py'), faac, faad, get_option('max-channels').to_string()], timeout: 120)
endif

if get_option('encoder') and get_option('decoder') and not meson.is_cross_build()
    if get_option('max-channels') >= 2 and encoder_ps and get_option('decoder-sbr') and get_option('decoder-ps')
        test('test_e2e_faac_ps_frontend', find_program('python3'),
            args: [files('test_faac_ps_frontend.py'), faac, faad,
                   get_option('max-channels').to_string()])
    endif
endif

if get_option('muxer') and get_option('encoder') and get_option('decoder') and not meson.is_cross_build()
    test('test_e2e_frontend_cli', find_program('python3'),
        args: [files('test_frontend_cli.py'), faac, faam, faad, get_option('max-channels').to_string()], timeout: 300)
    test('test_e2e_faad_frontend', find_program('python3'),
        args: [files('test_faad_frontend.py'), faac, faad, faam,
               get_option('max-channels').to_string(), get_option('decoder-sbr').to_string(),
               get_option('decoder-drc').to_string()])
    subdir('faad')

    test('test_e2e_faam_metadata_cli', find_program('python3'),
        args: [files('test_faam_metadata_cli.py'), faam, test_faam_metadata])
    test('test_e2e_faam_metadata_mutagen', find_program('python3'),
        args: [files('test_faam_metadata_mutagen.py'), faam, test_faam_metadata])
endif
