drc_probe = executable('faad_drc_probe', 'test_faad_drc_probe.c',
    include_directories: ['../..', '../../..', '../../../include', '../../../libfaad', common_inc],
    c_args: faad_c_args, link_with: libfaad_for_frontend)
# Both cases encode HE-AAC, which an LC-only encoder cannot do.
if encoder_sbr
    foreach case : ['he', 'hetilt']
        test('test_e2e_faad_drc_' + case, find_program('python3'),
            args: [files('test_faad_drc.py'), faac, drc_probe,
                   get_option('decoder-drc').to_string(), get_option('decoder-sbr').to_string(),
                   case, get_option('max-channels').to_string()])
    endforeach
endif
