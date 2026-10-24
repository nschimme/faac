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
if get_option('faar-precision') != 'float'
  faar_quality_groups += ['staged_short_symmetry', 'alias']
endif
foreach group : faar_quality_groups
  test('test_faar_quality_' + group, test_faar_quality, args: [group])
endforeach
foreach group : ['streaming-0', 'streaming-1']
  test('test_faar_quality_' + group, test_faar_quality,
    args: [group], is_parallel: false)
endforeach

test_faar_matrix = executable('test_faar_matrix', 'test_faar_matrix.c',
  c_args: libfaar_args,
  include_directories: ['..', inc],
  dependencies: [libfaar_dep, m_dep],
)
foreach group : ['matrix-0', 'matrix-1', 'matrix-2', 'equal-rate', 'channels', 'decimate']
  test('test_faar_matrix_' + group, test_faar_matrix,
    args: [group], is_parallel: false)
endforeach

test_faar_dft_fir = executable('test_faar_dft_fir',
  ['test_faar_dft_fir.c', '../dft_coeffs.c', '../dft_fir.c', '../fft_real.c'],
  c_args: libfaar_args,
  include_directories: ['..', inc],
  dependencies: [m_dep],
)
test('test_faar_dft_fir', test_faar_dft_fir)

test_faar_dft_prefilter = executable('test_faar_dft_prefilter',
  ['test_faar_dft_prefilter.c', '../dft_coeffs.c', '../dft_prefilter.c',
   '../dft_fir.c', '../fft_real.c'],
  c_args: libfaar_args,
  include_directories: ['..', inc],
  dependencies: [m_dep],
)
test('test_faar_dft_prefilter', test_faar_dft_prefilter)

# Float and double FFT coverage includes the real transform wrappers.
test_faar_fft_real = executable('test_faar_fft_real',
  ['test_faar_fft_real.c', '../fft_real.c'],
  include_directories: ['..', inc],
  dependencies: [m_dep],
)
foreach case : [
  ['test_faar_fft_real_256', 'real', '256'],
  ['test_faar_fft_real_1024', 'real', '1024'],
  ['test_faar_fft_real_8192', 'real', '8192'],
  ['test_faar_fft_real_65536', 'real', '65536'],
  ['test_faar_fft_complex_16', 'complex', '16'],
  ['test_faar_fft_complex_64', 'complex', '64'],
  ['test_faar_fft_complex_512', 'complex', '512'],
  ['test_faar_fft_complex_2048', 'complex', '2048'],
  ['test_faar_fft_complex_8192', 'complex', '8192'],
]
  test(case[0], test_faar_fft_real, args: [case[1], case[2]], timeout: 60)
endforeach

# Opt-in timing tool: no timing thresholds in the correctness suite.
if host_machine.system() != 'windows'
  executable('bench_faar', 'bench_faar.c',
    include_directories: inc,
    dependencies: [libfaar_dep, m_dep],
    build_by_default: false,
  )
  executable('bench_faar_open', 'bench_faar_open.c',
    include_directories: inc,
    dependencies: [libfaar_dep, m_dep],
    build_by_default: false,
  )
  threads_dep = dependency('threads')
  test_faar_fft_threads = executable('test_faar_fft_threads',
    ['test_faar_fft_threads.c', '../fft_real.c'],
    include_directories: ['..', inc],
    dependencies: [m_dep, threads_dep],
  )
  test('test_faar_fft_threads', test_faar_fft_threads, timeout: 120)
endif
