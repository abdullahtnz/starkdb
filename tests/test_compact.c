#include "test_common.h"

static void make_values(stark_db_t* db, int gen) {
    char v[256];
    for (int i = 0; i < 1000; i++) {
        snprintf(v, sizeof(v), "key-%d-generation-%d", i, gen);
        stark_add(db, (uint32_t)i, v, strlen(v) + 1);
    }
}

static void make_small_db(const char* p) {
    t_remove_all(p);
    stark_db_t* db = stark_open(p, 0);
    char v[64];
    for (int i = 0; i < 100; i++) {
        snprintf(v, sizeof(v), "value-%d", i);
        stark_add(db, (uint32_t)i, v, strlen(v) + 1);
    }
    stark_close(db);
}

static void write_compact_sentinel(const char* p) {
    char n[256];
    snprintf(n, sizeof(n), "%s.compact", p);
    FILE* f = fopen(n, "wb");
    if (f) { fprintf(f, "STARKCOMP\n"); fclose(f); }
}

static int verify_small_db(const char* p) {
    stark_db_t* db = stark_open(p, 0);
    T_ASSERT(db != NULL, "reopen");
    for (int i = 0; i < 100; i++) {
        T_ASSERT(t_stark_exists(db, (uint32_t)i), "key present");
    }
    stark_close(db);

    const char* leftovers[] = { ".compact", ".idx.new", ".dat.new", ".idx.old", ".dat.old" };
    char n[256];
    for (unsigned i = 0; i < 5; i++) {
        snprintf(n, sizeof(n), "%s%s", p, leftovers[i]);
        T_ASSERT(!t_file_exists(n), "temp file removed");
    }
    return 0;
}

int test_compact(void) {
    const char* p = "/tmp/stark_test_compact";
    t_remove_all(p);

    // ---- Correctness ----
    stark_db_t* db = stark_open(p, 0);
    T_ASSERT(db != NULL, "open");
    make_values(db, 0);
    for (int g = 1; g <= 5; g++) make_values(db, g);
    for (int i = 0; i < 200; i++) stark_delete(db, (uint32_t)i);
    stark_sync(db);

    long dat_before = t_file_size("/tmp/stark_test_compact.dat");
    long idx_before = t_file_size("/tmp/stark_test_compact.idx");

    // Compaction is rejected during a transaction.
    T_ASSERT(stark_begin(db) == STARK_OK, "begin");
    T_ASSERT(stark_compact(db) == STARK_ERROR, "compact rejected in txn");
    T_ASSERT(stark_rollback(db) == STARK_OK, "rollback");

    uint64_t before_bytes = 0, after_bytes = 0;
    T_ASSERT(stark_compact_stats(db, &before_bytes, &after_bytes) == STARK_OK, "compact_stats");

    long dat_after = t_file_size("/tmp/stark_test_compact.dat");
    long idx_after = t_file_size("/tmp/stark_test_compact.idx");
    T_ASSERT(dat_after < dat_before, "dat shrank");
    T_ASSERT(dat_after * 100 < dat_before * 35, "dat shrank ~>=65%");
    T_ASSERT(idx_after <= idx_before, "idx did not grow");
    T_ASSERT(before_bytes > 0 && after_bytes < before_bytes, "stats report shrink");

    // All live keys read back with latest values; deleted keys are gone.
    for (int i = 0; i < 1000; i++) {
        if (i < 200) {
            T_ASSERT(!t_stark_exists(db, (uint32_t)i), "deleted key gone");
        } else {
            char exp[256], buf[256];
            size_t s = sizeof(buf);
            snprintf(exp, sizeof(exp), "key-%d-generation-5", i);
            T_ASSERT(stark_get(db, (uint32_t)i, buf, &s) == STARK_OK, "live key get");
            T_ASSERT(strcmp(buf, exp) == 0, "latest value");
        }
    }

    // Temp files gone.
    T_ASSERT(!t_file_exists("/tmp/stark_test_compact.compact"), "sentinel gone");
    T_ASSERT(!t_file_exists("/tmp/stark_test_compact.idx.new"), "idx.new gone");
    T_ASSERT(!t_file_exists("/tmp/stark_test_compact.dat.new"), "dat.new gone");
    T_ASSERT(!t_file_exists("/tmp/stark_test_compact.idx.old"), "idx.old gone");
    T_ASSERT(!t_file_exists("/tmp/stark_test_compact.dat.old"), "dat.old gone");

    stark_close(db);

    // Reopen after compaction.
    db = stark_open(p, 0);
    T_ASSERT(db != NULL, "reopen after compact");
    T_ASSERT(t_stark_exists(db, 500), "read after reopen");
    stark_close(db);

    // Already-compact database: idempotent.
    db = stark_open(p, 0);
    long d1 = t_file_size("/tmp/stark_test_compact.dat");
    T_ASSERT(stark_compact(db) == STARK_OK, "compact again");
    long d2 = t_file_size("/tmp/stark_test_compact.dat");
    T_ASSERT(d2 <= d1, "no growth on recompact");
    stark_close(db);
    t_remove_all(p);

    // Single-key and empty databases.
    t_remove_all("/tmp/stark_test_compact_one");
    {
        stark_db_t* d = stark_open("/tmp/stark_test_compact_one", 0);
        T_ASSERT(d != NULL, "open one");
        stark_add(d, 1, "a", 2);
        T_ASSERT(stark_compact(d) == STARK_OK, "compact one key");
        T_ASSERT(t_stark_exists(d, 1), "one key survives");
        stark_close(d);
    }
    t_remove_all("/tmp/stark_test_compact_one");

    t_remove_all("/tmp/stark_test_compact_empty");
    {
        stark_db_t* d = stark_open("/tmp/stark_test_compact_empty", 0);
        T_ASSERT(d != NULL, "open empty");
        T_ASSERT(stark_compact(d) == STARK_OK, "compact empty");
        stark_close(d);
        T_ASSERT(t_file_size("/tmp/stark_test_compact_empty.idx") == 16 + 65536,
                 "empty idx minimal");
    }
    t_remove_all("/tmp/stark_test_compact_empty");

    // ---- Crash recovery at the four rename points ----
    char idx[256], dat[256], idxnew[256], datnew[256], idxold[256], datold[256];
    for (int state = 0; state <= 3; state++) {
        make_small_db(p);
        snprintf(idx, sizeof(idx), "%s.idx", p);
        snprintf(dat, sizeof(dat), "%s.dat", p);
        snprintf(idxnew, sizeof(idxnew), "%s.idx.new", p);
        snprintf(datnew, sizeof(datnew), "%s.dat.new", p);
        snprintf(idxold, sizeof(idxold), "%s.idx.old", p);
        snprintf(datold, sizeof(datold), "%s.dat.old", p);

        T_ASSERT(t_copy_file(idx, idxnew) == 0, "copy idx.new");
        T_ASSERT(t_copy_file(dat, datnew) == 0, "copy dat.new");
        if (state >= 1) rename(dat, datold);
        if (state >= 2) rename(idx, idxold);
        if (state >= 3) rename(datnew, dat);
        write_compact_sentinel(p);
        T_ASSERT(verify_small_db(p) == 0, "crash state recovered");
        t_remove_all(p);
    }

    // Crash right after sentinel, before .new files exist.
    make_small_db(p);
    write_compact_sentinel(p);
    T_ASSERT(verify_small_db(p) == 0, "no-new crash recovered");
    t_remove_all(p);

    // Bad sentinel magic -> open rejected.
    make_small_db(p);
    {
        char n[256];
        snprintf(n, sizeof(n), "%s.compact", p);
        FILE* f = fopen(n, "wb");
        T_ASSERT(f != NULL, "bad sentinel file");
        fprintf(f, "NOPE\n");
        fclose(f);
        stark_db_t* d = stark_open(p, 0);
        T_ASSERT(d == NULL, "bad compact sentinel rejected");
        if (d) stark_close(d);
        remove(n);
    }
    t_remove_all(p);

    // Multi-level persistence across reopen (root pinned at page 0).
    t_remove_all("/tmp/stark_test_persist");
    {
        stark_db_t* d = stark_open("/tmp/stark_test_persist", 0);
        T_ASSERT(d != NULL, "open persist");
        char v[64];
        for (int i = 0; i < 3000; i++) {
            snprintf(v, sizeof(v), "persist-%d", i);
            T_ASSERT(stark_add(d, (uint32_t)i, v, strlen(v) + 1) == STARK_OK, "persist add");
        }
        stark_close(d);
        d = stark_open("/tmp/stark_test_persist", 0);
        T_ASSERT(d != NULL, "reopen persist");
        for (int i = 0; i < 3000; i++) {
            T_ASSERT(t_stark_exists(d, (uint32_t)i), "persist key");
        }
        stark_stats_t s;
        T_ASSERT(stark_stats(d, &s) == STARK_OK, "persist stats");
        T_ASSERT(s.keys_count == 3000, "persist count");
        stark_close(d);
    }
    t_remove_all("/tmp/stark_test_persist");

    printf("  compaction tests passed\n");
    return 0;
}
