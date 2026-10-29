test_faam_args = faam_args + ['-DFAAM_HAVE_TAG_CHAPTER=1']

test_faam = executable('test_faam', 'test_faam.c',
  include_directories: [libfaam_inc, '..'],
  link_with: libfaam_static,
  c_args: test_faam_args + ['-DFAAM_STATIC'])
test('test_unit_faam_smoke', test_faam)

test_faam_audio_layout = executable('test_faam_audio_layout', 'test_faam_audio_layout.c',
  include_directories: [libfaam_inc, '..'],
  link_with: libfaam_static,
  c_args: ['-DFAAM_STATIC'])
test('test_unit_faam_audio_layout', test_faam_audio_layout)

test_faststart = executable('test_faststart', 'test_faststart.c',
  include_directories: [libfaam_inc, '..'],
  link_with: libfaam_static,
  c_args: faam_args + ['-DFAAM_STATIC'])
test('test_unit_faam_faststart', test_faststart)

test_faam_media = executable('test_faam_media', 'test_faam_media.c',
  include_directories: [libfaam_inc, '..'],
  link_with: libfaam_static,
  c_args: faam_args + ['-DFAAM_STATIC'])
test('test_unit_faam_media', test_faam_media)

test_faam_contract = executable('test_faam_contract', 'test_faam_contract.c',
  include_directories: [libfaam_inc, '..'],
  link_with: libfaam_static,
  c_args: faam_args + ['-DFAAM_STATIC'])
test('test_unit_faam_contract', test_faam_contract)

test_faam_abi = executable('test_faam_abi', 'test_faam_abi.c',
  include_directories: [libfaam_inc, '..'],
  link_with: libfaam_static,
  c_args: faam_args + ['-DFAAM_STATIC'])
foreach abi_case : ['outputs', 'future_inputs', 'track_limits_ids', 'odd_and_many', 'updates_close_flags', 'video', 'config_init', 'chapter_stride']
  test('test_unit_faam_abi_' + abi_case, test_faam_abi, args: [abi_case])
endforeach

if cc.has_argument('-fshort-enums')
  test_faam_abi_short = executable('test_faam_abi_short', 'test_faam_abi.c',
    include_directories: [libfaam_inc, '..'],
    link_with: libfaam_static,
    c_args: faam_args + ['-DFAAM_STATIC', '-fshort-enums'])
  test('test_unit_faam_abi_short_enums', test_faam_abi_short)
endif

test_faam_metadata = executable('test_faam_metadata', 'test_faam_metadata.c',
  include_directories: [libfaam_inc, '..'],
  link_with: libfaam_static,
  c_args: faam_args + ['-DFAAM_STATIC'])
test('test_unit_faam_metadata', test_faam_metadata)

# Links the library sources directly so the allocator hook counts every allocation.
faam_alloc_sanitized = get_option('b_sanitize') != 'none'
if not faam_alloc_sanitized
  force_include = (cc.get_argument_syntax() == 'msvc') ? '/FI' : '-include'
  test_faam_alloc = executable('test_faam_alloc',
    ['test_faam_alloc.c'] + libfaam_sources,
    include_directories: [libfaam_inc, '..'],
    c_args: faam_args + ['-DFAAM_STATIC', force_include, meson.current_source_dir() / 'test_faam_alloc.h'])
  test('test_unit_faam_alloc', test_faam_alloc)
endif
