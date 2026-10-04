quantize_simd_libs = []

if config_h.get('HAVE_SSE2')
    sse2_flag = (cc.get_argument_syntax() == 'msvc') ? ['/arch:SSE2'] : ['-msse2']
    quantize_sse = static_library('quantize_sse', 'quantize_sse.c',
        include_directories: ['..', '../include'],
        c_args: c_args + sse2_flag,
        gnu_symbol_visibility: 'hidden'
    )
    quantize_simd_libs += quantize_sse
endif

common_src = [
    'bitstream.c',
    'bitstream.h',
    'blockswitch.c',
    'blockswitch.h',
    'channels.c',
    'channels.h',
    'coder.h',
    'cpu_compute.c',
    'cpu_compute.h',
    'faac.c',
    'faac_internal.h',
    'filtbank.c',
    'filtbank.h',
    'frame.c',
    'frame.h',
    'huff2.c',
    'huff2.h',
    'huffdata.c',
    'huffdata.h',
    'quantize.c',
    'quantize.h',
    'ratecontrol.c',
    'ratecontrol.h',
    'sbr.c',
    'sbr.h',
    'sbr_internal.h',
    'sbr_bitstream.c',
    'sbr_tables.c',
    '../common/sbr_tables.c',
    'sbr_analysis.c',
    'sbr_analysis.h',
    'resample.c',
    'resample.h',
    'stereo.c',
    'stereo.h',
    'tns.c',
    'tns.h',
    'util.c',
    'util.h',
]

if encoder_ps
    common_src += ['ps.c', 'ps.h', '../common/ps_tables.c']
endif

libfaac = disabler()

# Derives library shared object version directly from project_version in root meson.build
project_ver = meson.project_version()

common_args = {
    'include_directories' : ['..', '../include', common_inc],
    'c_args' : c_args,
    'dependencies' : [libm, codec_dep],
    'link_with' : quantize_simd_libs,
    'install' : true,
    'gnu_symbol_visibility' : 'hidden',
}

default_lib = get_option('default_library')

if default_lib == 'static' or default_lib == 'both'
    libfaac_static = static_library(
        'faac',
        common_src,
        kwargs : common_args + {
          'override_options' : ['b_lto=false'],
        },
    )
endif

if default_lib == 'shared' or default_lib == 'both'
    libfaac_shared = shared_library(
        'faac',
        common_src,
        kwargs : common_args + {
          'version' : project_ver,
          'vs_module_defs' : 'libfaac.def',
        },
    )
endif

# Prefer the LTO shared library for the frontend and pkgconfig.
if default_lib == 'static'
    libfaac = libfaac_static
else
    libfaac = libfaac_shared
endif

pkgconfig = import('pkgconfig')
pkgconfig.generate(
    libfaac,
    name: 'FAAC',
    description: 'Freeware Advanced Audio Coder',
    version: project_ver,
    filebase: 'faac'
)

if encoder_ps
    test_ps = executable('test_ps', ['test_ps.c', 'ps.c', 'bitstream.c', '../common/ps_tables.c', '../common/fft.c'],
        include_directories: ['..', '../include', common_inc],
        c_args: c_args + [(cc.get_argument_syntax() == 'msvc') ? '/UNDEBUG' : '-UNDEBUG'], dependencies: [libm])
    test('faac_ps_test', test_ps)
endif

ps_api_test = executable('test_ps_api', 'test_ps_api.c',
    include_directories: ['..', '../include'], link_with: libfaac,
    c_args: c_args + [(cc.get_argument_syntax() == 'msvc') ? '/UNDEBUG' : '-UNDEBUG'])
test('faac_ps_api_test', ps_api_test)
