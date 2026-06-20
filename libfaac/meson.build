link_args = []
if host_machine.system() == 'windows' and meson.get_compiler('c').get_id() == 'gcc'
    # identifies mingw
    link_args += '-Wl,--add-stdcall-alias'
endif


quantize_simd_libs = []

if config_h.get('HAVE_SSE2')
    sse2_flag = (cc.get_argument_syntax() == 'msvc') ? ['/arch:SSE2'] : ['-msse2']
    quantize_sse = static_library('quantize_sse', 'quantize_sse.c',
        include_directories: ['..', '../include'],
        c_args: c_args + sse2_flag,
        gnu_symbol_visibility: 'hidden'
    )
    quantize_simd_libs += quantize_sse

    fft_sse = static_library('fft_sse', 'fft_sse.c',
        include_directories: ['..', '../include'],
        c_args: c_args + sse2_flag,
        gnu_symbol_visibility: 'hidden'
    )
    quantize_simd_libs += fft_sse
endif

if config_h.get('HAVE_AVX2')
    avx2_flag = (cc.get_argument_syntax() == 'msvc') ? ['/arch:AVX2'] : ['-mavx2']
    if config_h.get('HAVE_FMA')
        avx2_flag += (cc.get_argument_syntax() == 'msvc') ? [] : ['-mfma']
    endif
    fft_avx = static_library('fft_avx', 'fft_avx.c',
        include_directories: ['..', '../include'],
        c_args: c_args + avx2_flag,
        gnu_symbol_visibility: 'hidden'
    )
    quantize_simd_libs += fft_avx
endif

common_src = [
    'bitstream.c',
    'bitstream.h',
    'blockswitch.c',
    'blockswitch.h',
    'channels.c',
    'channels.h',
    'coder.h',
    'cpu_compute.c',
    'cpu_compute.h',
    'filtbank.c',
    'filtbank.h',
    'fft.c',
    'fft.h',
    'frame.c',
    'frame.h',
    'huff2.c',
    'huff2.h',
    'huffdata.c',
    'huffdata.h',
    'quantize.c',
    'quantize.h',
    'stereo.c',
    'stereo.h',
    'tns.c',
    'tns.h',
    'util.c',
    'util.h',
]

libfaac = library('faac', common_src,
    include_directories: ['..', '../include'],
    c_args: c_args,
    link_args: link_args,
    dependencies: [ libm ],
    link_with: quantize_simd_libs,
    vs_module_defs: 'libfaac.def',
    install: true,
    version: '0.0.0',
    gnu_symbol_visibility: 'hidden'
)


pkgconfig = import('pkgconfig')
pkgconfig.generate(
    libfaac,
    name: 'FAAC',
    description: 'Freeware Advanced Audio Coder',
    version: meson.project_version(),
    filebase: 'faac'
)
