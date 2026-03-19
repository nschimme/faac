python = import('python').find_installation('python3')
test_script = files('test_encoder.py')

test('encoder_sanity', python, args: [test_script, faac_exe.full_path()], timeout: 300)

test_api = executable('test_api', 'test_api.c',
    include_directories: '../include',
    link_with: libfaac
)
test('api_sanity', test_api)
