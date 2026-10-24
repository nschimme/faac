project('faac', 'c',
    version: '3.0.0',
    default_options: [
        'default_library=both',
        'buildtype=release',
        'warning_level=3',
        'c_std=gnu11,c11',
        'b_lto=true'
    ])

# Shared-library ABI version, separate from the project release version. This
# is the single source for the SONAMEs, and Meson checks that the public
# version macros in the installed headers match it.
library_abi_version = '3.0.0'
shared_library_args = {
    'version': library_abi_version,
    'soversion': library_abi_version.split('.')[0],
}

# Windows otherwise installs shared-library DLLs in bindir. Static frontends
# do not need those DLLs at runtime, so keep them with the other libraries.
if get_option('frontend') and get_option('frontend-static')
    shared_library_args += {
        'install_dir': get_option('libdir'),
    }
endif

cc = meson.get_compiler('c')
if cc.get_argument_syntax() == 'msvc'
    # _s variants buy nothing here and aren't portable; suppress instead.
    add_global_arguments('-D_CRT_SECURE_NO_WARNINGS', language: 'c')
endif

config_h = configuration_data()
config_h.set_quoted('PACKAGE', meson.project_name())
config_h.set_quoted('PACKAGE_VERSION', meson.project_version())

foreach h: ['getopt.h', 'immintrin.h']
    config_h.set('HAVE_' + h.underscorify().to_upper(), cc.has_header(h))
endforeach

config_h.set10('WORDS_BIGENDIAN', host_machine.endian() == 'big')

libm = cc.find_library('m', required: false)

cpu_family = target_machine.cpu_family()

config_h.set('HAVE_SSE2', (cpu_family in ['x86', 'x86_64']) and config_h.get('HAVE_IMMINTRIN_H'))
encoder_ps = get_option('encoder-ps') and get_option('max-channels') >= 2
config_h.set10('FAAC_ENCODER_PS', encoder_ps)

config_h.set('FAAC_SBR_DECIMATION', get_option('sbr-decimation'))

config_h.set('FAAC_STATS', get_option('stats'))
config_h.set('FAAD_STATS', get_option('stats'))

config_h.set('MAX_CHANNELS', get_option('max-channels'))

c_args = []
if cc.get_argument_syntax() == 'msvc'
    c_args += ['-DWIN32', '-DNDEBUG', '-D_WINDOWS', '-D_USRDLL']
endif

configure_file(
    output: 'config.h',
    configuration: config_h
)

# Force config.h into every TU instead of relying on each file's own include.
if cc.get_argument_syntax() == 'msvc'
    add_project_arguments('/FI' + meson.current_build_dir() / 'config.h', language: 'c')
else
    add_project_arguments('-include', meson.current_build_dir() / 'config.h', language: 'c')
endif

extra_args = cc.get_supported_arguments([
    '-Werror=unused-parameter',
    '/we4100',
])
add_project_arguments(extra_args, language: 'c')

subdir('docs')
subdir('include')
subdir('common')

subdir('tests/fuzz')

if get_option('encoder')
    subdir('libfaac')
endif
subdir('libfaam')

if get_option('decoder')
    subdir('libfaad')
endif

if get_option('frontend')
    subdir('frontend')
endif

# On Windows, verify both library variants link without import/export defines.
subdir('tests')
