project('faac', 'c',
    version: '3.0.0',
    default_options: [
        'default_library=both',
        'buildtype=release',
        'warning_level=3',
        'c_std=gnu11,c11',
        'b_lto=true'
    ])

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

subdir('docs')
subdir('include')
subdir('common')

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

# Consumers must link without any define, whichever flavour of a library they
# pick. Only Windows declares the API differently per flavour.
if host_machine.system() == 'windows'
    link_checks = []
    if is_variable('libfaac_shared')
        link_checks += [['faac', 'shared', libfaac_shared]]
    endif
    if is_variable('libfaac_static')
        link_checks += [['faac', 'static', libfaac_static]]
    endif
    if is_variable('libfaad_shared')
        link_checks += [['faad', 'shared', libfaad_shared]]
    endif
    if is_variable('libfaad_static')
        link_checks += [['faad', 'static', libfaad_static]]
    endif
    if is_variable('libfaam_shared')
        link_checks += [['faam', 'shared', libfaam_shared]]
    endif
    link_checks += [['faam', 'static', libfaam_static]]
    foreach c : link_checks
        test('link_' + c[0] + '_' + c[1],
            executable('link_check_' + c[0] + '_' + c[1], 'tests/link_check.c',
                include_directories : ['include'],
                c_args : ['-DCHECK_' + c[0].to_upper()],
                link_with : c[2]))
    endforeach
endif
