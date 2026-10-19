libfaar_sources = files(
  'polyresample.c',
  'dft_coeffs.c',
  'dft_prefilter.c',
  'dft_fir.c',
  'fft_real.c',
)

libfaar_args = ['-DFAAR_BUILDING']
if get_option('faar-coefficient-precision') == 'double'
  libfaar_args += '-DFAAR_DOUBLE_COEFFICIENTS'
endif

# Real-transform wrappers are exercised only by FFT verification.
libfaar_args += '-DFAAR_FFT_LEAN'

if host_machine.system() == 'windows'
  libfaar_args += '-DFAAR_DLL'
endif

m_dep = cc.find_library('m', required: false)

libfaar_shared = shared_library('faar',
  libfaar_sources,
  include_directories: inc,
  c_args: libfaar_args,
  dependencies: [m_dep],
  version: '3.0.0',
  soversion: '3',
  vs_module_defs: 'libfaar.def',
  install: true,
)

libfaar_static = static_library('faar_static',
  libfaar_sources,
  include_directories: inc,
  c_args: libfaar_args,
  dependencies: [m_dep],
  install: true,
)

if get_option('default_library') == 'shared'
  libfaar = libfaar_shared
else
  libfaar = libfaar_static
endif

libfaar_dep = declare_dependency(
  include_directories: inc,
  link_with: libfaar,
)

pkg = import('pkgconfig')
pkg.generate(libfaar,
  description: 'FAAC audio resampler library',
  subdirs: 'faac',
)

test_faar = executable('test_faar',
  'test_faar.c',
  include_directories: inc,
  dependencies: [libfaar_dep, m_dep],
)

test('test_faar', test_faar)

test_faar_quality = executable('test_faar_quality',
  'test_faar_quality.c',
  c_args: libfaar_args,
  include_directories: inc,
  dependencies: [libfaar_dep, m_dep],
)
test('test_faar_quality', test_faar_quality)

# Kept as independent verification tooling with Tuneout-derived test provenance.
faar_fingerprint_file = 'matrix-fingerprints-staged-short.txt'
if get_option('faar-coefficient-precision') == 'double'
  faar_fingerprint_file = 'matrix-fingerprints-staged-short-double.txt'
endif
faar_matrix_args = [
  '-DFAAR_RATE_FIXTURE_PATH="' + meson.current_source_dir() / 'verification/fixtures/rates.txt' + '"',
  '-DFAAR_FINGERPRINT_FIXTURE_PATH="' + meson.current_source_dir() / 'verification/fixtures' / faar_fingerprint_file + '"',
]
test_faar_tuneout_matrix = executable('test_faar_tuneout_matrix',
  'test_faar_tuneout_matrix.c',
  include_directories: inc,
  c_args: faar_matrix_args,
  dependencies: [libfaar_dep, m_dep],
)
test('test_faar_tuneout_matrix', test_faar_tuneout_matrix)

test_faar_dft_fir_sources = files(
  'test_faar_dft_fir.c',
  'dft_coeffs.c',
  'dft_fir.c',
  'fft_real.c',
)
test_faar_dft_fir = executable('test_faar_dft_fir',
  test_faar_dft_fir_sources,
  include_directories: inc,
  dependencies: [m_dep],
)
test('test_faar_dft_fir', test_faar_dft_fir)

test_faar_dft_prefilter_sources = files(
  'test_faar_dft_prefilter.c',
  'dft_coeffs.c',
  'dft_prefilter.c',
  'dft_fir.c',
  'fft_real.c',
)
test_faar_dft_prefilter = executable('test_faar_dft_prefilter',
  test_faar_dft_prefilter_sources,
  include_directories: inc,
  dependencies: [m_dep],
)
test('test_faar_dft_prefilter', test_faar_dft_prefilter)

# Float and double FFT verification, including the real transform wrappers.
test_faar_fft_real = executable('test_faar_fft_real',
  'test_faar_fft_real.c',
  'fft_real.c',
  include_directories: inc,
  dependencies: [m_dep],
)
test('test_faar_fft_real', test_faar_fft_real, timeout: 180)
