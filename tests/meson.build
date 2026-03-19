test_args = {
    'include_directories': ['..', '../include'],
    'link_with': faac_internal,
    'dependencies': [ libm ]
}

test_util = executable('test_util', 'test_util.c', kwargs: test_args)
test('util', test_util)

test_fft = executable('test_fft', 'test_fft.c', kwargs: test_args)
test('fft', test_fft)

test_channels = executable('test_channels', 'test_channels.c', kwargs: test_args)
test('channels', test_channels)

# These tests include the .c files directly to access static functions.
# They still link with faac_internal to resolve dependencies on other internal functions
# not included in the test source.
test_bitstream = executable('test_bitstream', 'test_bitstream.c', kwargs: test_args)
test('bitstream', test_bitstream)

test_tns = executable('test_tns', 'test_tns.c', kwargs: test_args)
test('tns', test_tns)

test_mdct = executable('test_mdct', 'test_mdct.c', kwargs: test_args)
test('mdct', test_mdct)

test_huffman = executable('test_huffman', 'test_huffman.c', kwargs: test_args)
test('huffman', test_huffman)

test_blockswitch = executable('test_blockswitch', 'test_blockswitch.c', kwargs: test_args)
test('blockswitch', test_blockswitch)

test_quantize = executable('test_quantize', 'test_quantize.c', kwargs: test_args)
test('quantize', test_quantize)

test_api = executable('test_api', 'test_api.c',
    include_directories: ['..', '../include'],
    link_with: libfaac,
    dependencies: [ libm ]
)
test('api', test_api)
