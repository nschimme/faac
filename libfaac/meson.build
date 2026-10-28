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

libfaac_src = [
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
    'ps.h',
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
    'sbr.h',
    'sbr_internal.h',
    'sbr_analysis.h',
    'resample.h',
    'stereo.c',
    'stereo.h',
    'tns.c',
    'tns.h',
    'util.c',
    'util.h',
]

encoder_codec_dep = codec_lc_dep
if encoder_sbr
    libfaac_src += files('sbr.c', 'sbr_analysis.c', 'sbr_bitstream.c', 'sbr_huff_tables.c', 'resample.c')
    encoder_codec_dep = codec_dep
    if encoder_ps
        libfaac_src += files('ps.c', 'ps_huff_tables.c', 'ps_huff_tables.h')
    endif
endif

libfaac = disabler()

common_args = {
    'include_directories' : ['..', '../include', common_inc],
    'c_args' : c_args + ['-DCOMMON_PREFIX=faac_'],
    'dependencies' : [libm, encoder_codec_dep],
    'link_with' : quantize_simd_libs,
    'install' : true,
    'gnu_symbol_visibility' : 'hidden',
}

default_lib = get_option('default_library')

# LTO on the static archive ships unportable compiler bytecode, so keep it off.
if default_lib == 'static' or default_lib == 'both' or (get_option('frontend') and get_option('frontend-static'))
    libfaac_static = static_library(
        'faac',
        libfaac_src,
        kwargs : common_args + {
          'override_options' : ['b_lto=false'],
        },
    )
endif

if default_lib == 'shared' or default_lib == 'both'
    shared_kwargs = shared_library_args + {
      'version': library_abi_versions['FAAC'],
      'soversion': library_abi_versions['FAAC'].split('.')[0],
      'c_args' : common_args['c_args'] + ['-DFAAC_BUILDING'],
      'vs_module_defs' : 'libfaac.def',
    }
    # GNU ld version script (ELF only). PE and Mach-O have no symbol versions, so skip them and probe the rest.
    faac_map = meson.current_source_dir() / 'libfaac.map'
    if host_machine.system() not in ['windows', 'cygwin', 'darwin'] and cc.has_link_argument('-Wl,--version-script=' + faac_map)
        shared_kwargs += {
          'link_args' : ['-Wl,--version-script=' + faac_map],
          'link_depends' : [faac_map],
        }
    endif
    libfaac_shared = shared_library(
        'faac',
        libfaac_src,
        kwargs : common_args + shared_kwargs,
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
    version: meson.project_version(),
    filebase: 'faac'
)

subdir('tests')
