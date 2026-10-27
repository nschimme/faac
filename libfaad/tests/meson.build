test_dab = executable(
    'test_dab_dec',
    'test_dab_dec.c',
    include_directories: ['..', '../..', '../../include', common_inc],
    c_args: faad_c_args,
    link_with: libfaad,
    dependencies: [libm, codec_dep],
)
test('test_unit_faad_dab_dec', test_dab)

# The tests link the archives directly; on Windows that must not import from a DLL.
test_static_args = default_lib == 'static' ? ['-DFAAD_STATIC', '-DFAAC_STATIC'] : []

test_faad = executable('test_faad', 'test_faad.c',
    c_args: faad_c_args + test_static_args,
    include_directories: ['..', '../..', '../../include', common_inc],
    link_with: [libfaad],
    dependencies: [dependency('threads', required: false)])
test('test_integration_faad_smoke', test_faad)

# Allocator interception conflicts with sanitizer interception.
faad_alloc_sanitized = get_option('b_sanitize') != 'none'
if host_machine.system() == 'linux' and not meson.is_cross_build() and (default_lib == 'static' or default_lib == 'both') and not faad_alloc_sanitized
    test_faad_alloc = executable('test_faad_alloc', 'test_faad_alloc.c',
        c_args: faad_c_args + ['-DFAAD_STATIC'],
        include_directories: ['..', '../..', '../../include', common_inc],
        link_with: [libfaad_static],
        link_args: ['-Wl,--wrap=malloc', '-Wl,--wrap=calloc', '-Wl,--wrap=free'])
    test('test_unit_faad_alloc', test_faad_alloc)
endif

test_faad_api = executable('test_faad_api', 'test_faad_api.c',
    c_args: faad_c_args + test_static_args,
    include_directories: ['..', '../..', '../../include', common_inc], link_with: [libfaad])
test('test_unit_faad_api', test_faad_api)

test_faad_asc = executable('test_faad_asc', 'test_faad_asc.c',
    c_args: faad_c_args + test_static_args,
    include_directories: ['..', '../..', '../../include', common_inc], link_with: [libfaad])
test('test_unit_faad_asc', test_faad_asc)

test_faad_tns_order = executable('test_faad_tns_order', 'test_faad_tns_order.c',
    c_args: faad_c_args + test_static_args,
    include_directories: ['..', '../..', '../../include', common_inc], link_with: [libfaad])
test('test_unit_faad_tns_order', test_faad_tns_order)

if get_option('decoder-sbr') and get_option('decoder-ps') and get_option('max-channels') >= 2
    test_faad_ps = executable('test_faad_ps', 'test_faad_ps.c',
        c_args: faad_c_args + ['-DFAAD_STATIC'],
        include_directories: ['..', '../..', '../../include', common_inc],
        link_with: [libfaad_static], dependencies: [libm, codec_dep])
    test('test_unit_faad_ps', test_faad_ps)
endif

test_faad_stereo = executable('test_faad_stereo', 'test_faad_stereo.c', '../stereo.c',
    c_args: faad_c_args + test_static_args,
    include_directories: ['..', '../..', '../../include', common_inc],
    dependencies: [libm, codec_dep])
test('test_unit_faad_stereo', test_faad_stereo)

test_faad_coupling = executable('test_faad_coupling', 'test_faad_coupling.c',
    c_args: faad_c_args + test_static_args,
    include_directories: ['..', '../..', '../../include', common_inc],
    link_with: [libfaad], dependencies: [libm])
test('test_unit_faad_coupling', test_faad_coupling)

if cc.has_argument('-fshort-enums')
    test_faad_api_short = executable('test_faad_api_short', 'test_faad_api.c',
        c_args: faad_c_args + test_static_args + ['-fshort-enums'],
        include_directories: ['..', '../..', '../../include', common_inc], link_with: [libfaad])
    test('test_unit_faad_api_short_enums', test_faad_api_short)
endif

if get_option('encoder')
    test_faad_robust = executable('test_faad_robust', 'test_faad_robust.c',
        c_args: faad_c_args + test_static_args,
        include_directories: ['..', '../..', '../../include', common_inc],
        link_with: [libfaad, libfaac], dependencies: [libm])
    foreach case_name : [
        'mono-lc', 'stereo-lc', 'stereo-he-aac', 'six-channel-lc',
        'three-channel-downmix', 'eight-channel-downmix', 'hang-cases'
    ]
        # These cases encode HE-AAC, which an LC-only encoder cannot do.
        if not encoder_sbr and (case_name == 'stereo-he-aac' or case_name == 'eight-channel-downmix')
            continue
        endif
        test('test_integration_faad_robust_' + case_name, test_faad_robust,
            args: [case_name], timeout: 120)
    endforeach

    test_faad_roundtrip = executable('test_faad_roundtrip', 'test_faad_roundtrip.c',
        c_args: faad_c_args + test_static_args,
        include_directories: ['..', '../..', '../../include', common_inc],
        link_with: [libfaad, libfaac], dependencies: [libm])
    foreach case_name : [
        'align-lc-44100', 'align-lc-48000', 'align-he-48000', 'align-he-32000',
        'tone-level-and-channels', 'silence', 'he-aac-band'
    ]
        # These cases encode HE-AAC, which an LC-only encoder cannot do.
        if not encoder_sbr and (case_name == 'align-he-48000' or case_name == 'align-he-32000' or case_name == 'he-aac-band')
            continue
        endif
        test('test_integration_faad_roundtrip_' + case_name, test_faad_roundtrip,
            args: [case_name], timeout: 120)
    endforeach

    test_faad_drc = executable('test_faad_drc', 'test_faad_drc.c',
        c_args: faad_c_args + test_static_args,
        include_directories: ['..', '../..', '../../include', common_inc],
        link_with: [libfaad, libfaac], dependencies: [libm])
    test('test_integration_faad_drc', test_faad_drc)
endif

if get_option('decoder-sbr') and (default_lib == 'static' or default_lib == 'both')
    test_sbr_workspace = executable('test_sbr_workspace', 'test_sbr_workspace.c',
        c_args: faad_c_args + ['-DFAAD_STATIC'],
        include_directories: ['..', '../..', '../../include', common_inc],
        link_with: [libfaad_static], dependencies: [libm])
    test('test_unit_faad_sbr_workspace', test_sbr_workspace)
endif
