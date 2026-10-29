#include "test_common.h"

int test_stats(void) {
    const char* p = "/tmp/stark_test_stats";
    t_remove_all(p);

    stark_db_t* db = stark_open(p, 0);
    T_ASSERT(db != NULL, "open");

    stark_stats_t s;
    T_ASSERT(stark_stats(db, &s) == STARK_OK, "stats fresh");
    T_ASSERT(s.keys_count == 0, "fresh count 0");
    T_ASSERT(s.btree_height == 1, "fresh height 1");

    char v[32];
    for (int i = 0; i < 100; i++) {
        snprintf(v, sizeof(v), "v%d", i);
        T_ASSERT(stark_add(db, (uint32_t)i, v, strlen(v) + 1) == STARK_OK, "insert 100");
    }
    T_ASSERT(stark_stats(db, &s) == STARK_OK, "stats 100");
    T_ASSERT(s.keys_count == 100, "count 100");

    for (int i = 100; i < 2000; i++) {
        snprintf(v, sizeof(v), "v%d", i);
        T_ASSERT(stark_add(db, (uint32_t)i, v, strlen(v) + 1) == STARK_OK, "insert 2000");
    }
    T_ASSERT(stark_stats(db, &s) == STARK_OK, "stats 2000");
    T_ASSERT(s.keys_count == 2000, "multi-leaf count 2000");
    T_ASSERT(s.btree_height >= 2, "multi-leaf height >= 2");

    // Overwrite counted once.
    T_ASSERT(stark_add(db, 1000, "zz", 3) == STARK_OK, "overwrite");
    T_ASSERT(stark_stats(db, &s) == STARK_OK, "stats overwrite");
    T_ASSERT(s.keys_count == 2000, "overwrite counted once");

    // Delete 50 of the first 100 -> total 1950.
    for (int i = 0; i < 50; i++) {
        T_ASSERT(stark_delete(db, (uint32_t)i) == STARK_OK, "delete");
    }
    T_ASSERT(stark_stats(db, &s) == STARK_OK, "stats delete");
    T_ASSERT(s.keys_count == 1950, "delete count");

    stark_close(db);
    t_remove_all(p);

    printf("  stats tests passed\n");
    return 0;
}
