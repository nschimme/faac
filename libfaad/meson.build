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
