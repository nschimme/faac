libfaar_sources = files(
  'polyresample.c',
)

libfaar_args = ['-DFAAR_BUILDING']

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
  description: 'Polyphase Farrow-Interpolated Kaiser-Sinc Audio Resampler Library',
  subdirs: 'faac',
)

test_faar = executable('test_faar',
  'test_faar.c',
  include_directories: inc,
  dependencies: [libfaar_dep, m_dep],
)

test('test_faar', test_faar)
