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

# Always built: the tests link its internals directly, and the frontend can
# link it statically. LTO on the archive ships unportable compiler bytecode,
# so keep it off.
libfaam_static = static_library(
  'faam',
  libfaam_sources,
  kwargs: libfaam_args + {
    'install': default_lib != 'shared',
    'override_options': ['b_lto=false'],
  },
)

if default_lib == 'shared' or default_lib == 'both'
  shared_kwargs = {
    'c_args': libfaam_args['c_args'] + ['-DFAAM_BUILDING'],
    # Same version as libfaac until the libraries are versioned together.
    'version': meson.project_version(),
    'vs_module_defs': 'libfaam.def',
    'install': true,
  }
  if get_option('frontend-static')
    shared_kwargs += {
      'install_dir': get_option('libdir'),
    }
  endif
  libfaam_shared = shared_library(
    'faam',
    libfaam_sources,
    kwargs: libfaam_args + shared_kwargs,
  )
endif

# Prefer the LTO shared library for the frontend and pkgconfig.
if default_lib == 'static'
  libfaam_lib = libfaam_static
else
  libfaam_lib = libfaam_shared
endif

libfaam_dep = declare_dependency(
  include_directories: libfaam_inc,
  link_with: libfaam_lib,
)

install_headers(libfaam_headers)

pkg = import('pkgconfig')
pkg.generate(
  libfaam_lib,
  name: 'faam',
  description: 'Freeware Advanced Audio Muxer',
  version: meson.project_version(),
)
