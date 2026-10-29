#include <stdio.h>

extern int test_header(void);
extern int test_stats(void);
extern int test_dtype(void);
extern int test_txn(void);
extern int test_compact(void);
extern int test_undefine(void);
extern int test_cursor_stale(void);
extern int test_lock(void);
extern int test_open_ex(void);
extern int test_backup(void);
extern int test_buffer_size(void);
extern int test_foreach_range(void);

typedef int (*test_fn)(void);

int main(void) {
    struct { const char* name; test_fn fn; } tests[] = {
        { "header",      test_header },
        { "stats",       test_stats },
        { "dtype",       test_dtype },
        { "txn",         test_txn },
        { "compact",     test_compact },
        { "undefine",    test_undefine },
        { "cursor_stale", test_cursor_stale },
        { "lock",         test_lock },
        { "open_ex",      test_open_ex },
        { "backup",       test_backup },
        { "buffer_size",  test_buffer_size },
        { "foreach_range", test_foreach_range },
    };

    int failed = 0;
    int total = (int)(sizeof(tests) / sizeof(tests[0]));
    for (int i = 0; i < total; i++) {
        printf("[ RUN  ] %s\n", tests[i].name);
        int rc = tests[i].fn();
        printf("[ %s ] %s\n", rc == 0 ? " OK " : "FAIL", tests[i].name);
        if (rc != 0) failed++;
    }

    printf("\n%d/%d tests passed\n", total - failed, total);
    return failed == 0 ? 0 : 1;
}
