/* Independent local test runner: failures must not hide later controls. */
#ifndef FAAR_TEST_GROUPS_H
#define FAAR_TEST_GROUPS_H
typedef struct { const char *name; int (*run)(void); } faar_test_group;
static int faar_run_groups(int argc, char **argv,
                           const faar_test_group *groups, size_t count) {
    int failed = 0;
    for (int arg = 1; arg < argc; ++arg) {
        size_t i;
        for (i = 0; i < count; ++i)
            if (!strcmp(argv[arg], groups[i].name)) break;
        if (i == count) { fprintf(stderr, "unknown test group: %s\n", argv[arg]); return 2; }
    }
    for (size_t i = 0; i < count; ++i) {
        int selected = argc == 1;
        for (int arg = 1; arg < argc; ++arg)
            if (!strcmp(argv[arg], groups[i].name)) selected = 1;
        if (!selected) continue;
        int result = groups[i].run();
        printf("GROUP %s %s\n", groups[i].name, result ? "FAIL" : "PASS");
        failed |= !!result;
    }
    return failed;
}
#endif
