libfaar_args = []
libfaar_float2_sources = []
libfaar_whole = []
m_dep = cc.find_library('m', required: false)
if get_option('faar-precision') == 'mixed'
  # The mixed policy dispatches per handle. Compile isolated float and double
  # engines with private symbol prefixes, then expose one public API facade.
  libfaar_hide_args = cc.get_supported_arguments(['-fvisibility=hidden'])
  libfaar_float_args = ['-DFAAR_FLOAT', '-DFAAR_BACKEND_FLOAT'] + libfaar_hide_args
  libfaar_double_args = ['-DFAAR_BACKEND_DOUBLE'] + libfaar_hide_args
  backend_float = static_library('faar_backend_float',
    ['polyresample.c', 'dft_prefilter.c', 'dft_fir.c', 'fft_float2.c',
     'backend_adapters.c'],
    include_directories: [inc, '.'],
    c_args: libfaar_float_args,
    dependencies: [m_dep],
    pic: true,
  )
  backend_double = static_library('faar_backend_double',
    ['polyresample.c', 'dft_prefilter.c', 'dft_fir.c', 'backend_adapters.c'],
    include_directories: [inc, '.'],
    c_args: libfaar_double_args,
    dependencies: [m_dep],
    pic: true,
  )
  libfaar_sources = files('backend_dispatch.c', 'dft_coeffs.c', 'fft_real.c')
  libfaar_whole = [backend_float, backend_double]
  libfaar_float2_sources = files('fft_float2.c')
else
  libfaar_sources = files(
    'polyresample.c',
    'dft_coeffs.c',
    'dft_prefilter.c',
    'dft_fir.c',
    'fft_real.c',
  )
endif

if get_option('faar-precision') == 'float'
  libfaar_args += ['-DFAAR_FLOAT']
  libfaar_float2_sources = files('fft_float2.c')
  libfaar_sources += libfaar_float2_sources
endif

if get_option('faar-precision') != 'mixed'
  libfaar_prepare_sources = libfaar_sources
endif

# Real-transform wrappers are exercised only by FFT verification.
if get_option('faar-precision') != 'mixed'
  libfaar_args += '-DFAAR_FFT_LEAN'
endif

default_lib = get_option('default_library')

# The FAAC frontend can be requested as a static executable even when the
# project's default library type is shared, so keep a static FAAR target for it.
if default_lib == 'static' or default_lib == 'both' or (get_option('frontend') and get_option('frontend-static'))
  libfaar_static = static_library('faar',
    libfaar_sources,
    include_directories: inc,
    c_args: libfaar_args,
    dependencies: [m_dep],
    link_whole: libfaar_whole,
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
    link_whole: libfaar_whole,
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
