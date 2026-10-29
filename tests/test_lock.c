#include "test_common.h"
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

int test_lock(void) {
    const char* p = "/tmp/stark_test_lock";
    const char* missing = "/tmp/stark_test_lock_missing";
    t_remove_all(p);
    t_remove_all(missing);

    // Fresh open reports OK and clears the global last-open error.
    stark_open_error_t err = STARK_OPEN_ERR_INVALID_ARG;
    stark_db_t* db = NULL;
    T_ASSERT(stark_open_ex(&db, p, 0, &err) == STARK_OK, "open fresh");
    T_ASSERT(db != NULL && err == STARK_OPEN_OK, "fresh handle + ok code");
    T_ASSERT(stark_last_open_error() == NULL, "no error after success");
    T_ASSERT(stark_add(db, 1, "one", 4) == STARK_OK, "seed key");

    // A second writer in the same process is rejected immediately.
    stark_db_t* second = NULL;
    err = STARK_OPEN_OK;
    T_ASSERT(stark_open_ex(&second, p, 0, &err) == STARK_ERROR, "second open fails");
    T_ASSERT(second == NULL, "no second handle");
    T_ASSERT(err == STARK_OPEN_ERR_LOCKED, "structured locked error");
    T_ASSERT(stark_last_open_error() != NULL &&
             strstr(stark_last_open_error(), "locked") != NULL,
             "locked message");

    // Compaction must hand the lock over to the new .idx inode.
    T_ASSERT(stark_compact(db) == STARK_OK, "compact while locked");
    second = NULL;
    err = STARK_OPEN_OK;
    T_ASSERT(stark_open_ex(&second, p, 0, &err) == STARK_ERROR,
             "second open fails after compaction");
    T_ASSERT(err == STARK_OPEN_ERR_LOCKED, "lock survives compaction");

    // A separate process must also be rejected.
    pid_t pid = fork();
    if (pid == 0) {
        stark_db_t* child_db = NULL;
        stark_open_error_t child_err = STARK_OPEN_OK;
        if (stark_open_ex(&child_db, p, 0, &child_err) != STARK_ERROR) _exit(10);
        if (child_db != NULL) _exit(11);
        if (child_err != STARK_OPEN_ERR_LOCKED) _exit(12);
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    T_ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0, "child sees lock");

    // Closing the writer releases the lock.
    stark_close(db);
    err = STARK_OPEN_ERR_INVALID_ARG;
    db = NULL;
    T_ASSERT(stark_open_ex(&db, p, 0, &err) == STARK_OK, "reopen after close");
    T_ASSERT(err == STARK_OPEN_OK, "reopen ok code");
    stark_close(db);

    // EXCL on a nonexistent database.
    err = STARK_OPEN_OK;
    db = NULL;
    T_ASSERT(stark_open_ex(&db, missing, STARK_OPEN_EXCL, &err) == STARK_ERROR,
             "EXCL missing fails");
    T_ASSERT(db == NULL && err == STARK_OPEN_ERR_NOT_FOUND, "EXCL not-found code");
    T_ASSERT(stark_last_open_error() != NULL &&
             strstr(stark_last_open_error(), "does not exist") != NULL,
             "EXCL message");
    T_ASSERT(!t_file_exists("/tmp/stark_test_lock_missing.idx"), "no idx created");
    T_ASSERT(!t_file_exists("/tmp/stark_test_lock_missing.dat"), "no dat created");

    // Read-only sharing semantics.
    stark_db_t* ro1 = stark_open(p, STARK_OPEN_READONLY);
    T_ASSERT(ro1 != NULL, "open read-only");
    char buf[64];
    size_t sz = sizeof(buf);
    T_ASSERT(stark_get(ro1, 1, buf, &sz) == STARK_OK, "read-only get works");

    T_ASSERT(stark_put(ro1, 2, "two", 4) == STARK_ERROR, "read-only put rejected");
    T_ASSERT(strstr(stark_error(ro1), "read-only") != NULL, "read-only message");
    T_ASSERT(stark_del_str(ro1, "x") == STARK_ERROR, "read-only delete rejected");
    T_ASSERT(stark_begin(ro1) == STARK_ERROR, "read-only begin rejected");

    stark_db_t* ro2 = stark_open(p, STARK_OPEN_READONLY);
    T_ASSERT(ro2 != NULL, "second read-only handle coexists");

    stark_db_t* writer = NULL;
    err = STARK_OPEN_OK;
    T_ASSERT(stark_open_ex(&writer, p, 0, &err) == STARK_ERROR,
             "writer rejected while readers exist");
    T_ASSERT(err == STARK_OPEN_ERR_LOCKED, "writer locked code");

    stark_close(ro2);
    writer = NULL;
    err = STARK_OPEN_OK;
    T_ASSERT(stark_open_ex(&writer, p, 0, &err) == STARK_ERROR,
             "writer still rejected with one reader");
    T_ASSERT(err == STARK_OPEN_ERR_LOCKED, "writer still locked");

    stark_close(ro1);
    writer = NULL;
    err = STARK_OPEN_OK;
    T_ASSERT(stark_open_ex(&writer, p, 0, &err) == STARK_OK,
             "writer succeeds after readers close");
    stark_close(writer);

    // Invalid arguments.
    err = STARK_OPEN_OK;
    T_ASSERT(stark_open_ex(NULL, p, 0, &err) == STARK_INVALID_ARG, "null out_db");
    T_ASSERT(stark_open_ex(&db, NULL, 0, &err) == STARK_INVALID_ARG, "null path");
    T_ASSERT(stark_open_ex(&db, p, 0, NULL) == STARK_INVALID_ARG, "null out_err");

    t_remove_all(p);
    t_remove_all(missing);
    printf("  lock tests passed\n");
    return 0;
}
