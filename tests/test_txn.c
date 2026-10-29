#include "test_common.h"
#include <unistd.h>
#include <sys/wait.h>

int test_txn(void) {
    const char* p = "/tmp/stark_test_txn";
    t_remove_all(p);

    stark_db_t* db = stark_open(p, 0);
    T_ASSERT(db != NULL, "open");

    // Rollback removes an inserted key.
    T_ASSERT(stark_begin(db) == STARK_OK, "begin");
    T_ASSERT(stark_add(db, 1, "x", 2) == STARK_OK, "add");
    T_ASSERT(stark_rollback(db) == STARK_OK, "rollback");
    T_ASSERT(!t_stark_exists(db, 1), "rollback removed insert");

    // Commit persists.
    T_ASSERT(stark_begin(db) == STARK_OK, "begin 2");
    T_ASSERT(stark_add(db, 1, "x", 2) == STARK_OK, "add 2");
    T_ASSERT(stark_commit(db) == STARK_OK, "commit");
    T_ASSERT(t_stark_exists(db, 1), "commit persisted");

    // Nested begin / commit-without-begin errors.
    T_ASSERT(stark_begin(db) == STARK_OK, "begin 3");
    T_ASSERT(stark_begin(db) == STARK_ERROR, "nested begin rejected");
    T_ASSERT(stark_rollback(db) == STARK_OK, "rollback 3");
    T_ASSERT(stark_commit(db) == STARK_ERROR, "commit without begin rejected");
    T_ASSERT(stark_rollback(db) == STARK_ERROR, "rollback without begin rejected");

    // Rollback spanning deletes + inserts.
    T_ASSERT(stark_begin(db) == STARK_OK, "begin 4");
    T_ASSERT(stark_add(db, 2, "two", 4) == STARK_OK, "add 2");
    T_ASSERT(stark_add(db, 3, "three", 6) == STARK_OK, "add 3");
    T_ASSERT(stark_delete(db, 1) == STARK_OK, "delete 1");
    T_ASSERT(stark_rollback(db) == STARK_OK, "rollback 4");
    T_ASSERT(t_stark_exists(db, 1), "restored deleted key");
    T_ASSERT(!t_stark_exists(db, 2) && !t_stark_exists(db, 3), "removed inserted keys");

    // Zero-write transaction.
    T_ASSERT(stark_begin(db) == STARK_OK, "begin 5");
    T_ASSERT(stark_rollback(db) == STARK_OK, "zero-write rollback");

    // Thousands of writes in one transaction (within the page-cache limit).
    T_ASSERT(stark_begin(db) == STARK_OK, "begin 6");
    char v[64];
    for (int i = 1000; i < 3000; i++) {
        snprintf(v, sizeof(v), "val-%d", i);
        T_ASSERT(stark_add(db, (uint32_t)i, v, strlen(v) + 1) == STARK_OK, "bulk add");
    }
    T_ASSERT(stark_rollback(db) == STARK_OK, "bulk rollback");
    T_ASSERT(!t_stark_exists(db, 1000) && !t_stark_exists(db, 2999), "bulk rolled back");
    stark_close(db);

    // Sentinel removed after success.
    {
        char n[256];
        snprintf(n, sizeof(n), "%s.txn", p);
        T_ASSERT(!t_file_exists(n), "sentinel deleted");
    }

    // Crash between begin and commit -> reopen rolls back.
    pid_t pid = fork();
    if (pid == 0) {
        stark_db_t* c = stark_open(p, 0);
        if (!c) _exit(2);
        if (stark_begin(c) != STARK_OK) _exit(3);
        stark_add(c, 42, "ghost", 6);
        _exit(0);  // exit without close/commit
    }
    int status = 0;
    waitpid(pid, &status, 0);
    T_ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0, "child exited cleanly");

    db = stark_open(p, 0);
    T_ASSERT(db != NULL, "reopen after crash");
    T_ASSERT(!t_stark_exists(db, 42), "crash transaction rolled back");
    T_ASSERT(t_stark_exists(db, 1), "pre-existing data intact after crash");
    stark_close(db);

    // Malformed sentinel -> open fails, no truncation.
    {
        char n[256];
        snprintf(n, sizeof(n), "%s.txn", p);
        FILE* f = fopen(n, "wb");
        T_ASSERT(f != NULL, "write bad sentinel");
        fprintf(f, "garbage\nnope\nBAD\n");
        fclose(f);

        db = stark_open(p, 0);
        T_ASSERT(db == NULL, "malformed sentinel rejected");
        if (db) stark_close(db);
        remove(n);
    }

    db = stark_open(p, 0);
    T_ASSERT(db != NULL && t_stark_exists(db, 1), "usable after removing sentinel");
    if (db) stark_close(db);

    t_remove_all(p);
    printf("  transaction tests passed\n");
    return 0;
}
