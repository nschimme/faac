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
    'c_args': libfaam_args['c_args'] + ['-DFAAM_STATIC'],
    'install': default_lib != 'shared',
    'override_options': ['b_lto=false'],
  },
)

if default_lib == 'shared' or default_lib == 'both'
  libfaam_shared = shared_library(
    'faam',
    libfaam_sources,
    kwargs: libfaam_args + {
      'c_args': libfaam_args['c_args'] + ['-DFAAM_BUILDING'],
      # Same version as libfaac until the libraries are versioned together.
      'version': '3.0.0',
      'vs_module_defs': 'libfaam.def',
      'install': true,
    },
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

test_faam_args = faam_args + ['-DFAAM_HAVE_TAG_CHAPTER=1']

test_faam = executable(
  'test_faam',
  'test_faam.c',
  include_directories: libfaam_inc,
  link_with: libfaam_static,
  c_args: test_faam_args + ['-DFAAM_STATIC'],
)

test('faam_smoke_test', test_faam)

test_master_layout = executable(
  'test_master_layout',
  'test_master_layout.c',
  include_directories: libfaam_inc,
  link_with: libfaam_static,
  c_args: ['-DFAAM_STATIC'],
)
test('faam_master_layout_test', test_master_layout)

test_faam_media = executable(
  'test_faam_media',
  'test_faam_media.c',
  include_directories: libfaam_inc,
  link_with: libfaam_static,
  c_args: faam_args + ['-DFAAM_STATIC'],
)
test('faam_media_test', test_faam_media)

test_faam_contract = executable(
  'test_faam_contract',
  'test_faam_contract.c',
  include_directories: libfaam_inc,
  link_with: libfaam_static,
  c_args: faam_args + ['-DFAAM_STATIC'],
)
test('faam_contract_test', test_faam_contract)

test_faam_abi = executable(
  'test_faam_abi',
  'test_faam_abi.c',
  include_directories: libfaam_inc,
  link_with: libfaam_static,
  c_args: faam_args + ['-DFAAM_STATIC'],
)
foreach abi_case : ['outputs', 'future_inputs', 'track_limits_ids', 'odd_and_many', 'metadata', 'updates_close_flags', 'video']
  test('faam_abi_' + abi_case + '_test', test_faam_abi, args: [abi_case])
endforeach

# Links the library sources directly so the allocator hook in test_faam_alloc.h
# counts every allocation; the library itself is built without it.
force_include = (cc.get_argument_syntax() == 'msvc') ? '/FI' : '-include'
test_faam_alloc = executable(
  'test_faam_alloc',
  ['test_faam_alloc.c'] + libfaam_sources,
  include_directories: libfaam_inc,
  c_args: faam_args + ['-DFAAM_STATIC', force_include, meson.current_source_dir() / 'test_faam_alloc.h'],
)
test('faam_alloc_test', test_faam_alloc)
