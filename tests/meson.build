test_inc = include_directories('..', '../include')

# Shared configuration for unit tests
test_env_args = {
    'include_directories': test_inc,
    'dependencies': [ libm ]
}

# Internal component tests use header inclusion to access internal functions.
# They link against the internal static library to resolve these symbols.
internal_tests = [
    'util',
    'bitstream',
    'fft',
    'channels',
    'tns',
    'mdct',
    'huffman',
    'blockswitch',
    'quantize'
]

foreach t : internal_tests
    exe = executable('test_' + t, 'test_' + t + '.c',
        include_directories: test_env_args['include_directories'],
        dependencies: test_env_args['dependencies'],
        link_with: faac_static
    )
    test(t, exe)
endforeach

# Public API test links against the official shared library
test_api = executable('test_api', 'test_api.c',
    include_directories: test_env_args['include_directories'],
    dependencies: test_env_args['dependencies'],
    link_with: libfaac
)
test('api', test_api)
