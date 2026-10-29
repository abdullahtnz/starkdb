#include "test_common.h"

int test_cursor_stale(void) {
    const char* p = "/tmp/stark_test_cursor";
    t_remove_all(p);

    stark_db_t* db = stark_open(p, 0);
    T_ASSERT(db != NULL, "open");
    for (int i = 0; i < 5; i++) {
        char v[16];
        snprintf(v, sizeof(v), "v%d", i);
        T_ASSERT(stark_add(db, (uint32_t)i, v, strlen(v) + 1) == STARK_OK, "seed");
    }

    // Iterate three times with no writes.
    for (int round = 0; round < 3; round++) {
        stark_cursor_t* c = stark_cursor_create(db);
        T_ASSERT(c != NULL, "cursor create");
        int n = 0;
        for (stark_result_t r = stark_cursor_first(c); r == STARK_OK;
             r = stark_cursor_next(c)) {
            uint32_t k;
            char b[16];
            size_t s = sizeof(b);
            T_ASSERT(stark_cursor_get(c, &k, b, &s) == STARK_OK, "cursor get");
            n++;
        }
        T_ASSERT(n == 5, "iterate 5 keys");
        stark_cursor_destroy(c);
    }

    // Read, write, then read again -> stale.
    {
        stark_cursor_t* c = stark_cursor_create(db);
        T_ASSERT(stark_cursor_first(c) == STARK_OK, "first");
        uint32_t k;
        char b[16];
        size_t s = sizeof(b);
        T_ASSERT(stark_cursor_get(c, &k, b, &s) == STARK_OK, "get before write");
        T_ASSERT(stark_add(db, 100, "z", 2) == STARK_OK, "intervening write");
        T_ASSERT(stark_cursor_next(c) == STARK_STALE_CURSOR, "next stale");
        T_ASSERT(stark_cursor_get(c, &k, b, &s) == STARK_STALE_CURSOR, "get stale");
        T_ASSERT(strstr(stark_error(db), "cursor invalidated") != NULL, "stale error message");
        stark_cursor_destroy(c);  // must not crash
    }

    // cursor_first on a populated database succeeds.
    {
        stark_cursor_t* c = stark_cursor_create(db);
        T_ASSERT(stark_cursor_first(c) == STARK_OK, "first ok");
        stark_cursor_destroy(c);
    }

    // Advancing past the end yields NOT_FOUND, not stale.
    {
        stark_cursor_t* c = stark_cursor_create(db);
        T_ASSERT(stark_cursor_first(c) == STARK_OK, "first");
        stark_result_t r;
        while ((r = stark_cursor_next(c)) == STARK_OK) { }
        T_ASSERT(r == STARK_NOT_FOUND, "past end NOT_FOUND");
        stark_cursor_destroy(c);
    }

    // Multiple cursors all become stale after one write.
    {
        stark_cursor_t* a = stark_cursor_create(db);
        stark_cursor_t* b = stark_cursor_create(db);
        T_ASSERT(stark_cursor_first(a) == STARK_OK, "a first");
        T_ASSERT(stark_cursor_first(b) == STARK_OK, "b first");
        T_ASSERT(stark_add(db, 200, "z", 2) == STARK_OK, "write");
        T_ASSERT(stark_cursor_next(a) == STARK_STALE_CURSOR, "a stale");
        T_ASSERT(stark_cursor_next(b) == STARK_STALE_CURSOR, "b stale");
        stark_cursor_destroy(a);
        stark_cursor_destroy(b);
    }

    // A zero-write rollback rebuilds the tree; cursors must be invalidated.
    {
        stark_cursor_t* c = stark_cursor_create(db);
        T_ASSERT(stark_cursor_first(c) == STARK_OK, "first pre-rollback");
        T_ASSERT(stark_begin(db) == STARK_OK, "begin");
        T_ASSERT(stark_rollback(db) == STARK_OK, "rollback");
        T_ASSERT(stark_cursor_next(c) == STARK_STALE_CURSOR,
                 "stale after zero-write rollback");
        stark_cursor_destroy(c);
    }

    // Closing the DB takes precedence: ops return CLOSED, not stale.
    {
        stark_cursor_t* c = stark_cursor_create(db);
        T_ASSERT(stark_cursor_first(c) == STARK_OK, "first before close");
        stark_close(db);
        T_ASSERT(stark_cursor_next(c) == STARK_CLOSED, "closed beats stale (next)");
        T_ASSERT(stark_cursor_first(c) == STARK_CLOSED, "closed (first)");
        uint32_t k;
        char b[8];
        size_t s = sizeof(b);
        T_ASSERT(stark_cursor_get(c, &k, b, &s) == STARK_CLOSED, "closed (get)");
        stark_cursor_destroy(c);
    }

    t_remove_all(p);
    printf("  cursor staleness tests passed\n");
    return 0;
}
