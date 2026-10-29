libfaar_args = []
libfaar_float2_sources = []
m_dep = cc.find_library('m', required: false)
libfaar_sources = files(
  'polyresample.c',
  'dft_coeffs.c',
  'dft_prefilter.c',
  'dft_fir.c',
  'fft.c',
)

if get_option('resampler-float')
  libfaar_float2_sources = files('fft_float2.c')
  libfaar_sources += libfaar_float2_sources
else
  libfaar_args += ['-DFAAR_WIDE_HIGH']
endif
libfaar_prepare_sources = libfaar_sources

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
  faar_kwargs = shared_library_args
  # GNU ld version script (ELF only). PE and Mach-O have no symbol versions, so skip them and probe the rest.
  faar_map = meson.current_source_dir() / 'libfaar.map'
  if host_machine.system() not in ['windows', 'cygwin', 'darwin'] and cc.has_link_argument('-Wl,--version-script=' + faar_map)
    faar_kwargs += {
      'link_args': ['-Wl,--version-script=' + faar_map],
      'link_depends': [faar_map],
    }
  endif
  libfaar_shared = shared_library('faar',
    libfaar_sources,
    include_directories: inc,
    c_args: libfaar_args + ['-DFAAR_BUILDING'],
    dependencies: [m_dep],
    kwargs: faar_kwargs,
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
