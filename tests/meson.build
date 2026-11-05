# Fuzz harnesses are cross-component, so they live here rather than under a
# single library's tests directory.
subdir('fuzz')

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

# Static archives must not collide in a consumer's link. nm cannot read MSVC .lib files.
nm = find_program('nm', required: false)
if nm.found() and cc.get_argument_syntax() != 'msvc'
    archive_paths = []
    archive_targets = []
    foreach c : link_checks
        if c[1] == 'static'
            archive_paths += [c[2].full_path()]
            archive_targets += [c[2]]
        endif
    endforeach
    if archive_paths.length() > 0
        test('test_static_symbol_collisions', find_program('check_symbol_collisions.py'),
            args: [nm.full_path()] + archive_paths,
            depends: archive_targets)
    endif
endif

# Compile-time guard for the public struct layouts; the checks are _Static_asserts.
test('test_abi_layout',
    executable('test_abi_layout', 'test_abi_layout.c',
        include_directories: ['../include']))
