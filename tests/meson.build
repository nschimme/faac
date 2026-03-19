python = import('python').find_installation('python3')
test_script = files('test_encoder.py')

test('encoder_sanity', python, args: [test_script, faac_exe.full_path()], timeout: 300)
