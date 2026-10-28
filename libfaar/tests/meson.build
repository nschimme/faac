test_faar = executable('test_faar', 'test_faar.c',
  c_args: libfaar_args,
  include_directories: ['..', inc],
  dependencies: [libfaar_dep, m_dep],
)
test('test_faar', test_faar)

test_faar_api = executable('test_faar_api', 'test_faar_api.c',
  c_args: libfaar_args,
  include_directories: ['..', inc],
  dependencies: [libfaar_dep, m_dep],
)
test('test_faar_api', test_faar_api)

test_faar_quality = executable('test_faar_quality', 'test_faar_quality.c',
  c_args: libfaar_args,
  include_directories: ['..', inc],
  dependencies: [libfaar_dep, m_dep],
)
faar_quality_groups = [
  'boundary', 'lengths', 'impulses', 'passband',
  '48k_96k_wideband', '48k_32k_wideband', 'empty', 'dither',
]
faar_quality_groups += ['staged_short_symmetry', 'alias', 'high_stopband', 'high_up_residual']
foreach group : faar_quality_groups
  test('test_faar_quality_' + group, test_faar_quality, args: [group])
endforeach
foreach group : ['streaming-0', 'streaming-1']
  test('test_faar_quality_' + group, test_faar_quality, args: [group])
endforeach

test_faar_matrix = executable('test_faar_matrix', 'test_faar_matrix.c',
  c_args: libfaar_args,
  include_directories: ['..', inc],
  dependencies: [libfaar_dep, m_dep],
)
foreach group : ['matrix-0', 'matrix-1', 'matrix-2', 'equal-rate', 'channels', 'decimate']
  test('test_faar_matrix_' + group, test_faar_matrix, args: [group])
endforeach

test_faar_dft_fir = executable('test_faar_dft_fir',
  ['test_faar_dft_fir.c', '../dft_coeffs.c', '../dft_fir.c', '../fft.c'] + libfaar_float2_sources,
  c_args: libfaar_args,
  include_directories: ['..', inc],
  dependencies: [m_dep],
)
test('test_faar_dft_fir', test_faar_dft_fir)

test_faar_dft_shared = executable('test_faar_dft_shared',
  ['test_faar_dft_shared.c', '../dft_fir.c', '../fft.c'] + libfaar_float2_sources,
  c_args: libfaar_args,
  include_directories: ['..', inc],
  dependencies: [m_dep],
)
test('test_faar_dft_shared', test_faar_dft_shared)

faar_force_include = (cc.get_argument_syntax() == 'msvc') ? '/FI' : '-include'
faar_alloc_include = meson.current_source_dir() / 'faar_alloc_hooks.h'
test_faar_bounded = executable('test_faar_bounded',
  ['test_faar_bounded.c', 'faar_alloc_hooks.c'] + libfaar_prepare_sources,
  c_args: libfaar_args + [faar_force_include, faar_alloc_include],
  include_directories: ['.', '..', inc],
  dependencies: [m_dep],
)
test('test_faar_bounded', test_faar_bounded, timeout: 120)

test_faar_design = executable('test_faar_design', 'test_faar_design.c',
  c_args: libfaar_args,
  include_directories: ['..', inc],
  dependencies: [libfaar_dep, m_dep, dependency('threads', required: false)],
)
test('test_faar_design', test_faar_design, timeout: 180)

test_faar_design_alloc = executable('test_faar_design_alloc',
  ['test_faar_design.c', 'faar_alloc_hooks.c'] + libfaar_prepare_sources,
  c_args: libfaar_args + [faar_force_include, faar_alloc_include, '-DFAAR_PLAN_ALLOC_TEST'],
  include_directories: ['.', '..', inc],
  dependencies: [m_dep],
)
test('test_faar_design_alloc', test_faar_design_alloc, timeout: 180)

test_faar_dft_prefilter = executable('test_faar_dft_prefilter',
  ['test_faar_dft_prefilter.c', '../dft_coeffs.c', '../dft_prefilter.c',
   '../dft_fir.c', '../fft.c'] + libfaar_float2_sources,
  c_args: libfaar_args,
  include_directories: ['..', inc],
  dependencies: [m_dep],
)
test('test_faar_dft_prefilter', test_faar_dft_prefilter)

# Float and double FFT coverage; the double transform is otherwise built only
# for native-double High.
test_faar_fft = executable('test_faar_fft',
  ['test_faar_fft.c', '../fft.c'],
  c_args: ['-DFAAR_WIDE_HIGH'],
  include_directories: ['..', inc],
  dependencies: [m_dep],
)
foreach case : [
  ['test_faar_fft_complex_16', 'complex', '16'],
  ['test_faar_fft_complex_64', 'complex', '64'],
  ['test_faar_fft_complex_512', 'complex', '512'],
  ['test_faar_fft_complex_2048', 'complex', '2048'],
  ['test_faar_fft_complex_4096', 'complex', '4096'],
  ['test_faar_fft_complex_8192', 'complex', '8192'],
  ['test_faar_fft_complex_16384', 'complex', '16384'],
  ['test_faar_fft_complex_32768', 'complex', '32768'],
  ['test_faar_fft_complex_65536', 'complex', '65536'],
]
  test(case[0], test_faar_fft, args: [case[1], case[2]], timeout: 60)
endforeach

if get_option('resampler-float')
  test_faar_float2 = executable('test_faar_float2',
    ['test_faar_float2.c', '../fft_float2.c', '../fft.c'],
    c_args: ['-DFAAR_WIDE_HIGH'],
    include_directories: ['..', inc],
    dependencies: [m_dep],
  )
  test('test_faar_float2', test_faar_float2)
endif
