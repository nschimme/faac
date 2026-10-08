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

if get_option('decoder-sbr')
    faad_src += ['sbr_dec_tables.c']
endif

faad_c_args = c_args + ['-DCOMMON_PREFIX=faad_']
if not get_option('decoder-sbr')
    faad_c_args += ['-DFAAD_DISABLE_SBR']
endif
if not get_option('decoder-ps') or not get_option('decoder-sbr') or get_option('max-channels') < 2
    faad_c_args += ['-DFAAD_DISABLE_PS']
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
        'faad',
        faad_src,
        kwargs : common_args + {
          'c_args' : common_args['c_args'] + ['-DFAAD_STATIC'],
          'override_options' : ['b_lto=false'],
        },
    )
endif

if default_lib == 'shared' or default_lib == 'both'
    libfaad_shared = shared_library(
        'faad',
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
    filebase: 'faad'
)

# The tests link the archives directly; on Windows that must not import from a DLL.
test_static_args = default_lib == 'static' ? ['-DFAAD_STATIC', '-DFAAC_STATIC'] : []

test_faad = executable('test_faad',
    'test_faad.c',
    c_args: faad_c_args + test_static_args,
    include_directories: ['..', '../include'],
    link_with: [libfaad],
    dependencies: [dependency('threads', required: false)],
)
test('faad_smoke_test', test_faad)

test_faad_api = executable('test_faad_api', 'test_faad_api.c',
    c_args: faad_c_args + test_static_args,
    include_directories: ['..', '../include', common_inc], link_with: [libfaad])
test('faad_api_test', test_faad_api)

test_faad_asc = executable('test_faad_asc', 'test_faad_asc.c',
    c_args: faad_c_args + test_static_args,
    include_directories: ['..', '../include'], link_with: [libfaad])
test('faad_asc_test', test_faad_asc)
if cc.has_argument('-fshort-enums')
    test_faad_api_short = executable('test_faad_api_short', 'test_faad_api.c',
        c_args: faad_c_args + test_static_args + ['-fshort-enums'],
        include_directories: ['..', '../include', common_inc], link_with: [libfaad])
    test('faad_api_short_enums_test', test_faad_api_short)
endif

if get_option('stats')
    test_faad_dump = executable('test_faad_dump',
        'test_faad_dump.c',
        c_args: faad_c_args + test_static_args,
        include_directories: ['..', '../include'],
        link_with: [libfaad],
    )
    test('faad_dump_test', find_program('python3'),
        args: [files('../tests/faad_dump_test.py'), test_faad_dump])
endif

if get_option('encoder')
    test_faad_robust = executable('test_faad_robust',
        'test_faad_robust.c',
        c_args: test_static_args,
        include_directories: ['..', '../include', common_inc],
        link_with: [libfaad, libfaac],
        dependencies: [libm],
    )
    test('faad_robust_test', test_faad_robust, timeout: 120)
endif

if get_option('encoder') and (default_lib == 'static' or default_lib == 'both') and not meson.is_cross_build()
    test('static_archives_test', find_program('python3'),
        args: [files('../tests/static_archives_test.py'), libfaac_static.full_path(), libfaad_static.full_path()])
endif
