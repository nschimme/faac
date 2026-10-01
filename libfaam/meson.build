# Meson build file for libfaam

libfaam_sources = files(
  'demux.c',
  'mux.c',
  'metadata.c',
  'util.c',
)

# tag.c/chapter.c are standalone retrofit APIs (faam_update_tags_stream /
# faam_update_chapters_stream) that neither demux.c nor mux.c calls into --
# safe to drop on flash-constrained targets that don't need iTunes metadata
# or QuickTime chapter retrofitting. atom_patch.c is their shared in-place
# atom-resize primitive.
if not get_option('embedded')
    libfaam_sources += files('tag.c', 'chapter.c', 'atom_patch.c')
endif

libfaam_headers = files(
  '../include/faam.h',
)

libfaam_inc = include_directories('../include', '../libfaac', '../common')

faam_args = []
if get_option('embedded')
    faam_args += ['-DFAAM_EMBEDDED=1']
endif
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

# Static only: measured smaller than one shared copy for both the encoder and
# decoder deployments, since LTO and dead-stripping keep just what each
# consumer uses.
libfaam_lib = static_library(
  'faam',
  libfaam_sources,
  include_directories: libfaam_inc,
  c_args: ffunc_args + faam_args,
  gnu_symbol_visibility: 'hidden',
  install: true,
)

libfaam_dep = declare_dependency(
  include_directories: libfaam_inc,
  link_with: libfaam_lib,
)

install_headers(libfaam_headers)

pkg = import('pkgconfig')
pkg.generate(
  libfaam_lib,
  name: 'faam',
  description: 'Freeware Advanced Audio & Video Muxer Library',
  version: '1.0.0',
)

test_faam_args = faam_args
if not get_option('embedded')
    test_faam_args += ['-DFAAM_HAVE_TAG_CHAPTER=1']
endif

test_faam = executable(
  'test_faam',
  'test_faam.c',
  include_directories: libfaam_inc,
  link_with: libfaam_lib,
  c_args: test_faam_args,
)

test('faam_smoke_test', test_faam)

test_master_layout = executable(
  'test_master_layout',
  'test_master_layout.c',
  include_directories: libfaam_inc,
  link_with: libfaam_lib,
)
test('faam_master_layout_test', test_master_layout)

test_faam_media = executable(
  'test_faam_media',
  'test_faam_media.c',
  include_directories: libfaam_inc,
  link_with: libfaam_lib,
  c_args: faam_args,
)
test('faam_media_test', test_faam_media)

# Links the library sources directly so the allocator hook in alloc_hook/faam_alloc.h
# counts every allocation; the library itself is built without it.
if get_option('muxer-fragmented')
    test_faam_alloc = executable(
      'test_faam_alloc',
      ['test_faam_alloc.c'] + libfaam_sources,
      include_directories: [include_directories('alloc_hook'), libfaam_inc],
      c_args: faam_args + ['-DFAAM_CUSTOM_ALLOC=1'],
    )
    test('faam_alloc_test', test_faam_alloc)
endif
