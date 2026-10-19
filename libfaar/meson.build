libfaar_sources = files(
  'polyresample.c',
)

if get_option('faar-backend') != 'polyphase'
  libfaar_sources += files(
    'dft_coeffs.c',
    'dft_prefilter.c',
    'dft_fir.c',
  )
endif
if get_option('faar-backend') == 'staged'
  libfaar_sources += files('fft_double.c')
endif
if get_option('faar-backend') == 'staged-short'
  libfaar_sources += files('fft_real.c')
endif

libfaar_args = ['-DFAAR_BUILDING']
if get_option('faar-coefficient-precision') == 'double'
  libfaar_args += '-DFAAR_DOUBLE_COEFFICIENTS'
endif
if get_option('faar-backend') != 'polyphase'
  libfaar_args += '-DFAAR_STAGED_BACKEND'
endif
if get_option('faar-backend') == 'staged-short'
  libfaar_args += '-DFAAR_STAGED_SHORT_POLY'
  libfaar_args += '-DFAAR_PAIRED_FFT'
  libfaar_args += '-DFAAR_FFT_LEAN'
endif

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
faar_fingerprint_file = 'matrix-fingerprints.txt'
if get_option('faar-coefficient-precision') == 'double'
  faar_fingerprint_file = 'matrix-fingerprints-double.txt'
endif
if get_option('faar-backend') == 'staged'
  faar_fingerprint_file = 'matrix-fingerprints-staged.txt'
  if get_option('faar-coefficient-precision') == 'double'
    faar_fingerprint_file = 'matrix-fingerprints-staged-double.txt'
  endif
endif
if get_option('faar-backend') == 'staged-short'
  faar_fingerprint_file = 'matrix-fingerprints-staged-short.txt'
  if get_option('faar-coefficient-precision') == 'double'
    faar_fingerprint_file = 'matrix-fingerprints-staged-short-double.txt'
  endif
endif
faar_matrix_args = [
  '-DFAAR_RATE_FIXTURE_PATH="' + meson.current_source_dir() / 'verification/fixtures/rates.txt' + '"',
  '-DFAAR_FINGERPRINT_FIXTURE_PATH="' + meson.current_source_dir() / 'verification/fixtures' / faar_fingerprint_file + '"',
]
if get_option('faar-backend') == 'staged-short'
  # The independent coefficient oracle must model the selected backend even
  # though this test executable links to the already-built library.
  faar_matrix_args += '-DFAAR_STAGED_SHORT_POLY'
endif
test_faar_tuneout_matrix = executable('test_faar_tuneout_matrix',
  'test_faar_tuneout_matrix.c',
  include_directories: inc,
  c_args: faar_matrix_args,
  dependencies: [libfaar_dep, m_dep],
)
test('test_faar_tuneout_matrix', test_faar_tuneout_matrix)

# Independent double FFT core for the CPU DFT resampling stage. It is linked
# into libfaar for staged and staged-short builds.
test_faar_fft_sources = files('test_faar_fft.c', 'fft_double.c')
test_faar_fft_args = []
test_faar_fft = executable('test_faar_fft',
  test_faar_fft_sources,
  include_directories: inc,
  c_args: test_faar_fft_args,
  dependencies: [m_dep],
)
test('test_faar_fft', test_faar_fft)

test_faar_dft_fir_sources = files(
  'test_faar_dft_fir.c',
  'dft_coeffs.c',
  'dft_fir.c',
  'fft_double.c',
)
test_faar_dft_fir_args = []
if get_option('faar-backend') == 'staged-short'
  test_faar_dft_fir_sources += files('fft_real.c')
  test_faar_dft_fir_args += '-DFAAR_TEST_UNPAIRED'
  test_faar_dft_fir_args += '-DFAAR_PAIRED_FFT'
endif
test_faar_dft_fir = executable('test_faar_dft_fir',
  test_faar_dft_fir_sources,
  c_args: test_faar_dft_fir_args,
  include_directories: inc,
  dependencies: [m_dep],
)
test('test_faar_dft_fir', test_faar_dft_fir)

test_faar_dft_prefilter_sources = files(
  'test_faar_dft_prefilter.c',
  'dft_coeffs.c',
  'dft_prefilter.c',
  'dft_fir.c',
  'fft_double.c',
)
test_faar_dft_prefilter_args = []
if get_option('faar-backend') == 'staged-short'
  test_faar_dft_prefilter_sources += files('fft_real.c')
  test_faar_dft_prefilter_args += '-DFAAR_PAIRED_FFT'
endif
test_faar_dft_prefilter = executable('test_faar_dft_prefilter',
  test_faar_dft_prefilter_sources,
  c_args: test_faar_dft_prefilter_args,
  include_directories: inc,
  dependencies: [m_dep],
)
test('test_faar_dft_prefilter', test_faar_dft_prefilter)

# Single-precision FFT used by the paired DFT stage; libfaar links it only for
# staged-short, but the verification builds it in every backend.
test_faar_fft_real = executable('test_faar_fft_real',
  'test_faar_fft_real.c',
  'fft_real.c',
  include_directories: inc,
  dependencies: [m_dep],
)
test('test_faar_fft_real', test_faar_fft_real, timeout: 180)
