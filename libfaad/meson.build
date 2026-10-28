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
    shared_kwargs = shared_library_args + {
      'c_args' : common_args['c_args'] + ['-DFAAD_BUILDING'],
    }
    # GNU ld version script (ELF only). PE and Mach-O have no symbol versions, so skip them and probe the rest.
    faad_map = meson.current_source_dir() / 'libfaad.map'
    if host_machine.system() not in ['windows', 'cygwin', 'darwin'] and cc.has_link_argument('-Wl,--version-script=' + faad_map)
        shared_kwargs += {
          'link_args' : ['-Wl,--version-script=' + faad_map],
          'link_depends' : [faad_map],
        }
    endif
    libfaad_shared = shared_library(
        'faad3',
        faad_src,
        kwargs : common_args + shared_kwargs,
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
    version: meson.project_version(),
    filebase: 'faad3'
)

subdir('tests')
