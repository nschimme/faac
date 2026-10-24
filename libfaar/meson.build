libfaar_args = []
libfaar_float2_sources = []
m_dep = cc.find_library('m', required: false)
libfaar_sources = files(
  'polyresample.c',
  'dft_coeffs.c',
  'dft_prefilter.c',
  'dft_fir.c',
  'fft_real.c',
)

if get_option('faar-precision') == 'mixed'
  libfaar_args += ['-DFAAR_WIDE_HIGH']
else
  libfaar_float2_sources = files('fft_float2.c')
  libfaar_sources += libfaar_float2_sources
endif
libfaar_prepare_sources = libfaar_sources

# Real-transform wrappers are exercised only by FFT verification.
libfaar_args += '-DFAAR_FFT_LEAN'

default_lib = get_option('default_library')

# The FAAC frontend can be requested as a static executable even when the
# project's default library type is shared, so keep a static FAAR target for it.
if default_lib == 'static' or default_lib == 'both' or (get_option('frontend') and get_option('frontend-static'))
  libfaar_static = static_library('faar',
    libfaar_sources,
    include_directories: inc,
    c_args: libfaar_args,
    dependencies: [m_dep],
    override_options: ['b_lto=false'],
    install: default_lib != 'shared',
  )
endif

if default_lib == 'shared' or default_lib == 'both'
  libfaar_shared = shared_library('faar',
    libfaar_sources,
    include_directories: inc,
    c_args: libfaar_args + ['-DFAAR_BUILDING'],
    dependencies: [m_dep],
    kwargs: shared_library_args,
    gnu_symbol_visibility: 'hidden',
    vs_module_defs: 'libfaar.def',
    install: true,
  )
endif

if default_lib == 'static'
  libfaar = libfaar_static
else
  libfaar = libfaar_shared
endif

libfaar_dep = declare_dependency(
  include_directories: inc,
  link_with: libfaar,
)

pkgconfig = import('pkgconfig')
pkgconfig.generate(
  libfaar,
  name: 'FAAR',
  description: 'Freeware Advanced Audio Resampler',
  version: meson.project_version(),
  filebase: 'faar',
)

subdir('tests')
