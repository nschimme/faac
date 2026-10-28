# Verify each library variant links with every exported function, so the symbol
# map and .def cannot fall behind the header. On Windows this also proves the
# import/export defines are not needed.
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
if is_variable('libfaam_static')
    link_checks += [['faam', 'static', libfaam_static]]
endif
if is_variable('libfaar_shared')
    link_checks += [['faar', 'shared', libfaar_shared]]
endif
if is_variable('libfaar_static')
    link_checks += [['faar', 'static', libfaar_static]]
endif
foreach c : link_checks
    test('test_link_' + c[0] + '_' + c[1],
        executable('link_check_' + c[0] + '_' + c[1], 'test_link_check.c',
            include_directories: ['../include'],
            c_args: ['-DCHECK_' + c[0].to_upper()],
            link_with: c[2]))
endforeach

# Compile-time guard for the public struct layouts; the checks are _Static_asserts.
test('test_abi_layout',
    executable('test_abi_layout', 'test_abi_layout.c',
        include_directories: ['../include']))
