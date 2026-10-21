faad_src = [
    'decoder.c',
    'bits.c',
    'asc.c',
    'huffman.c',
    'syntax.c',
    'dequant.c',
    'stereo.c',
    'tns.c',
    'imdct.c',
    'sbr.c',
    'ps.c',
]

# Small mono/stereo builds omit the rarely used AAC coupling tool.
if get_option('max-channels') >= 3
    faad_src += ['coupling.c']
endif

if get_option('decoder-sbr')
    faad_src += ['sbr_dec_tables.c']
endif

faad_c_args = c_args + ['-DCOMMON_PREFIX=faad_']
if not get_option('decoder-drc')
    faad_c_args += ['-DFAAD_DISABLE_DRC']
endif
if not get_option('decoder-sbr')
    faad_c_args += ['-DFAAD_DISABLE_SBR']
endif
if not get_option('decoder-ps') or not get_option('decoder-sbr') or get_option('max-channels') < 2
    faad_c_args += ['-DFAAD_DISABLE_PS']
endif
if not get_option('decoder-dab')
    faad_c_args += ['-DFAAD_DISABLE_DAB']
endif

project_ver = meson.project_version()

common_args = {
    'include_directories' : ['..', '../include', common_inc],
    'c_args' : faad_c_args,
    'dependencies' : [libm, codec_dep],
    'install' : true,
    'gnu_symbol_visibility' : 'hidden',
}

default_lib = get_option('default_library')

if default_lib == 'static' or default_lib == 'both' or (get_option('frontend') and get_option('frontend-static'))
    libfaad_static = static_library(
        'faad3',
        faad_src,
        kwargs : common_args + {
          'override_options' : ['b_lto=false'],
        },
    )
endif

if default_lib == 'shared' or default_lib == 'both'
    libfaad_shared = shared_library(
        'faad3',
        faad_src,
        kwargs : common_args + {
          'c_args' : common_args['c_args'] + ['-DFAAD_BUILDING'],
          'version' : '3.0.0',
          'soversion' : '3',
        },
    )
endif

if default_lib == 'static'
    libfaad = libfaad_static
else
    libfaad = libfaad_shared
endif

pkgconfig = import('pkgconfig')
pkgconfig.generate(
    libfaad,
    name: 'FAAD',
    description: 'Freeware Advanced Audio Decoder',
    version: '3.0.0',
    filebase: 'faad3'
)

test_dab = executable(
    'test_dab_dec',
    'test_dab_dec.c',
    include_directories : ['..', '../include', common_inc],
    c_args : faad_c_args,
    link_with : libfaad,
    dependencies : [libm, codec_dep],
)
test('test_dab_dec', test_dab)

test_faad_fft = executable('test_faad_fft', 'test_faad_fft.c',
    include_directories : ['..', '../include', common_inc],
    c_args : faad_c_args,
    dependencies : [libm, codec_dep],
)
test('faad_fft_test', test_faad_fft)


# The tests link the archives directly; on Windows that must not import from a DLL.
test_static_args = default_lib == 'static' ? ['-DFAAD_STATIC', '-DFAAC_STATIC'] : []

test_faad = executable('test_faad',
    'test_faad.c',
    c_args: faad_c_args + test_static_args,
    include_directories: ['..', '../include', common_inc],
    link_with: [libfaad],
    dependencies: [dependency('threads', required: false)],
)
test('faad_smoke_test', test_faad)

if host_machine.system() == 'linux' and (default_lib == 'static' or default_lib == 'both')
    test_faad_alloc = executable('test_faad_alloc', 'test_faad_alloc.c',
        c_args: faad_c_args + ['-DFAAD_STATIC'],
        include_directories: ['..', '../include', common_inc],
        link_with: [libfaad_static],
        link_args: ['-Wl,--wrap=malloc', '-Wl,--wrap=calloc', '-Wl,--wrap=free'])
    test('faad_alloc_test', test_faad_alloc)
endif

test_faad_api = executable('test_faad_api', 'test_faad_api.c',
    c_args: faad_c_args + test_static_args,
    include_directories: ['..', '../include', common_inc], link_with: [libfaad])
test('faad_api_test', test_faad_api)

test_faad_asc = executable('test_faad_asc', 'test_faad_asc.c',
    c_args: faad_c_args + test_static_args,
    include_directories: ['..', '../include', common_inc], link_with: [libfaad])
test('faad_asc_test', test_faad_asc)

test_faad_tns_order = executable('test_faad_tns_order', 'test_faad_tns_order.c',
    c_args: faad_c_args + test_static_args,
    include_directories: ['..', '../include', common_inc], link_with: [libfaad])
test('faad_tns_order_test', test_faad_tns_order)

test_faad_stereo = executable('test_faad_stereo',
    'test_faad_stereo.c', 'stereo.c',
    c_args: faad_c_args + test_static_args,
    include_directories: ['..', '../include', common_inc],
    dependencies: [libm, codec_dep])
test('faad_stereo_test', test_faad_stereo)

test_faad_coupling = executable('test_faad_coupling', 'test_faad_coupling.c',
    c_args: faad_c_args + test_static_args,
    include_directories: ['..', '../include', common_inc],
    link_with: [libfaad], dependencies: [libm])
test('faad_coupling_test', test_faad_coupling)


if cc.has_argument('-fshort-enums')
    test_faad_api_short = executable('test_faad_api_short', 'test_faad_api.c',
        c_args: faad_c_args + test_static_args + ['-fshort-enums'],
        include_directories: ['..', '../include', common_inc], link_with: [libfaad])
    test('faad_api_short_enums_test', test_faad_api_short)
endif

if get_option('encoder')
    test_faad_robust = executable('test_faad_robust',
        'test_faad_robust.c',
        c_args: faad_c_args + test_static_args,
        include_directories: ['..', '../include', common_inc],
        link_with: [libfaad, libfaac],
        dependencies: [libm],
    )
    test('faad_robust_test', test_faad_robust, timeout: 120)

    test_faad_roundtrip = executable('test_faad_roundtrip',
        'test_faad_roundtrip.c',
        c_args: faad_c_args + test_static_args,
        include_directories: ['..', '../include', common_inc],
        link_with: [libfaad, libfaac],
        dependencies: [libm],
    )
    test('faad_roundtrip_test', test_faad_roundtrip)
endif

if get_option('encoder') and (default_lib == 'static' or default_lib == 'both') and not meson.is_cross_build()
    test('static_archives_test', find_program('python3'),
        args: [files('../tests/static_archives_test.py'), libfaac_static.full_path(), libfaad_static.full_path()],
        depends: [libfaac_static, libfaad_static])
endif

if get_option('decoder-sbr') and (default_lib == 'static' or default_lib == 'both')
    test_sbr_workspace = executable('test_sbr_workspace', 'test_sbr_workspace.c',
        c_args: faad_c_args + ['-DFAAD_STATIC'],
        include_directories: ['..', '../include', common_inc],
        link_with: [libfaad_static],
        dependencies: [libm])
    test('faad_sbr_workspace_test', test_sbr_workspace)
endif
