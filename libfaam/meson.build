# Meson build file for libfaam

libfaam_sources = files(
  'demux.c',
  'mux.c',
  'metadata.c',
  'faam_util.c',
  'tag.c',
  'chapter.c',
  'atom_patch.c',
)

libfaam_headers = files(
  '../include/faam.h',
)

libfaam_inc = include_directories('../include', '../common')

faam_args = []
if get_option('muxer-video')
    faam_args += ['-DFAAM_MUXER_VIDEO=1']
endif
if get_option('muxer-fragmented')
    faam_args += ['-DFAAM_MUXER_FRAGMENTED=1']
endif

ffunc_args = []
if cc.has_argument('-ffunction-sections')
    ffunc_args += ['-ffunction-sections', '-fdata-sections']
endif

libfaam_args = {
  'include_directories': libfaam_inc,
  'c_args': ffunc_args + faam_args,
  'gnu_symbol_visibility': 'hidden',
}

default_lib = get_option('default_library')

# The tests link FAAM internals directly, so this archive is always built.
# The frontend also uses it when frontend-static is enabled. LTO on the
# archive ships unportable compiler bytecode, so keep it off.
libfaam_static = static_library(
  'faam',
  libfaam_sources,
  kwargs: libfaam_args + {
    'install': default_lib != 'shared',
    'override_options': ['b_lto=false'],
  },
)

if default_lib == 'shared' or default_lib == 'both'
  shared_kwargs = shared_library_args + {
    'version': library_abi_versions['FAAM'],
    'soversion': library_abi_versions['FAAM'].split('.')[0],
    'c_args': libfaam_args['c_args'] + ['-DFAAM_BUILDING'],
    'vs_module_defs': 'libfaam.def',
    'install': true,
  }
  # GNU ld version script (ELF only). PE and Mach-O have no symbol versions, so skip them and probe the rest.
  faam_map = meson.current_source_dir() / 'libfaam.map'
  if host_machine.system() not in ['windows', 'cygwin', 'darwin'] and cc.has_link_argument('-Wl,--version-script=' + faam_map)
    shared_kwargs += {
      'link_args': ['-Wl,--version-script=' + faam_map],
      'link_depends': [faam_map],
    }
  endif
  libfaam_shared = shared_library(
    'faam',
    libfaam_sources,
    kwargs: libfaam_args + shared_kwargs,
  )
endif

# Select the library used by the frontend and pkg-config according to
# default_library, matching FAAR, FAAC, and FAAD.
if default_lib == 'static'
  libfaam = libfaam_static
else
  libfaam = libfaam_shared
endif

libfaam_dep = declare_dependency(
  include_directories: libfaam_inc,
  link_with: libfaam,
)

install_headers(libfaam_headers)

pkgconfig = import('pkgconfig')
pkgconfig.generate(
  libfaam,
  name: 'FAAM',
  description: 'Freeware Advanced Audio Muxer',
  version: meson.project_version(),
  filebase: 'faam',
)

subdir('tests')
